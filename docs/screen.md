# 屏幕控制

> **验证说明**：本文基于对 `zte_topsw_devui`、`libzte_SDKowrt.so` 与内核 `qpic_display` 的静态分析，以及在设备上的只读检查（DRM sysfs、`/sys/kernel/debug/qpic_display/`、`/proc/kallsyms`）。下列写操作**已在设备上执行**：修改 `/usr/ui/language/*.ini` 并重启 UI；向 `/sys/kernel/debug/qpic_display/draw` 写入颜色码；向 `image_dump` 写入（未刷出自定义帧，且曾导致整机重启）；legacy `drmModeSetCrtc` / `SETCRTC`（导致 QPIC 超时重启）；运行 `files/qpic_demo/` DRM Atomic 演示；读写 `/sys/class/leds/led:lcd/brightness`；调用 `ubus call zwrt_bsp.led set`（请求超时）。再次执行前须备份分区或相关文件，并自行确认副作用。

触摸屏 UI 由 `zte_topsw_devui` 实现。显示栈为 **DRM**（`/dev/dri/card0`）经高通 **QPIC** 驱动，将 LVGL 画面输出至 320×480 LCD。系统无 fbdev 节点（无 `/dev/fb0`）。

## 相关程序

| 文件 | 说明 |
|---|---|
| `/usr/bin/zte_topsw_devui` | 触摸屏 UI 主程序（LVGL + FreeType + libpng + libdrm），procd 管理 |
| `/etc/init.d/zte_topsw_devui` | 启动脚本，`START=48`，`respawn` |
| `/usr/ui/startdui.sh` | 手动启动脚本（`killall` 后再起 `zte_topsw_devui`） |
| `/usr/ui/startanimation.sh` | 开机动画入口（会先杀掉 `zte_topsw_devui`） |
| `mtdev2tuio` | 触摸输入：`/dev/input/event3` → TUIO UDP `127.0.0.1:3333` |
| `/usr/lib/libzte_SDKowrt.so` | ZTE SDK：LCD 开关/亮度、ZBUS 消息、与 UI 的 SysV 消息队列 |
| `/usr/lib/libdrm.so.2` | DRM 用户态库（`drmModeAddFB2` 等） |
| `zte_topsw_devui_common_u60pro` | opkg 包，提供 `/usr/ui/` 资源与二进制 |

二进制可备份到 `playground/nfc_analysis/zte_topsw_devui`（与 NFC 分析同目录习惯，该目录在 `.gitignore` 中排除）。

## 硬件 / 内核

| 项目 | 值 |
|---|---|
| 分辨率 | **320×480** |
| 像素格式 | RGB565（一帧 **307200** 字节） |
| DRM 设备 | `/dev/dri/card0` |
| DRM connector | `card0-DIN-1`，modes=`320x480`，status=`connected` |
| 驱动模块 | `qpic_display`（内置，无独立 `.ko`） |
| 硬件节点 | `1c98000.qcom,msm_qpic` |
| 触摸 | Sitronix ST77921（I2C 0x55，MT-B 2 点）；`/dev/input/event3` |
| fbdev | **无** `/dev/fb0` |

内核 debugfs（需 root）：

```text
/sys/kernel/debug/qpic_display/
├── draw          # 写颜色码，全屏纯色填充（有效）
├── image_dump    # 调试用，不能当「写自定义帧」接口（危险）
├── is_panel_on   # 读：1=亮屏
└── reg           # 寄存器调试
```

关键内核符号（`/proc/kallsyms`）：`qpic_panel_draw`、`qpic_send_frame`、`qpic_display_debugfs_draw`、`qpic_display_debugfs_image_dump_wr` 等。

## 工作原理

```text
触摸 /dev/input/event3
        │
        ▼
   mtdev2tuio ──TUIO──► zte_topsw_devui (LVGL)
                              ▲
 ZBUS / libzte_SDKowrt.so ────┘  (WiFi/充电/按键/SIM 等事件)
                              │
                              ▼
                     /dev/dri/card0 (DRM)
                              │
                              ▼
                     qpic_display → 320×480 LCD
```

- UI 资源在 `/usr/ui/`：`anim/`（开机/充电动画 PNG）、`skin/`、`fonts/`、`language/*.ini`、`miniuiconfig.ini`。
- 文案取自语言 ini（如 `TUFormIDLE_LabelText_Remind`），并非写死在二进制中。
- SDK 通过 ZBUS / SysV 消息队列与 `zte_topsw_devui` 通信；`showMessage` 等是 UI 内部函数，**没有**对外的「显示任意字符串」ubus API。
- ubus 对象 `zwrt_deviceui` 方法很少，主要是触摸状态、直供电模式等，不能用来画屏。

### SDK 里和 LCD 相关的接口（节选）

存在于 `libzte_SDKowrt.so`，供 UI/其他模块调用，不是「画图」API：

- `ZTD_SetLcdOpenOrClose` / `ZTD_SetLcdOpenOrCloseByFile` / `ZTD_SetLcdOpenOrCloseForDeviceUI`
- `ZTD_SetLcdBrightness` / `ZTD_GetLcdBrightness` / `ZTD_GetNVLcdBrightness`
- `ZTD_SetLcdShowInterval` / `ZTD_SetLcdShowIntervalTemporarily`
- `ZTD_SetLcdTouchStatus` / `ZTD_SetLcdLockStatus`
- `ZTD_SetWakeUpScreenStatus` 等

`ZTD_OutputUIEmergLog` / `ZTD_OutputUIlog` 主要是日志，不是往屏幕弹文字。

## UI 资源与改字

> **写操作，已在设备上执行。** 语言文件可写（`/usr` 已 overlay 到 `/data` 时修改会持久化）。执行前须备份对应 ini。

```sh
# 主界面底部提示改成 hello（改完需重启 UI）
sed -i 's/TUFormIDLE_LabelText_Remind=".*"/TUFormIDLE_LabelText_Remind="hello"/' \
  /usr/ui/language/English.ini
/etc/init.d/zte_topsw_devui restart
```

恢复示例：

```sh
sed -i 's/TUFormIDLE_LabelText_Remind="hello"/TUFormIDLE_LabelText_Remind="* Data usage is for reference only."/' \
  /usr/ui/language/English.ini
/etc/init.d/zte_topsw_devui restart
```

开机动画见 [bootanim.md](bootanim.md)（`/usr/ui/anim/`，320×480 PNG）。

## 向屏幕输出画面

### 方法 1：修改语言文件

适合修改 UI 中已有文案。见上一节。

### 方法 2：debugfs 纯色填充（仅调试）

> **写操作，已在设备上执行。** 须先停止 UI，否则画面会被 `zte_topsw_devui` 覆盖。

```sh
/etc/init.d/zte_topsw_devui stop
killall -9 zte_topsw_devui mtdev2tuio 2>/dev/null

# 颜色码：0=蓝 1=白 2=红 3=绿 4=黑
echo 2 > /sys/kernel/debug/qpic_display/draw

# 恢复 UI
/etc/init.d/zte_topsw_devui start
```

在设备上执行：写入 `2` 后屏幕全红，说明 `draw` → `qpic_panel_draw` 路径有效。

### 方法 3：`image_dump` 写入自定义 RGB565（无效且危险）

> **写操作，已在设备上执行。** 不可将 `image_dump` 当作写帧接口。

```sh
# 错误示范，不要再用
dd if=/tmp/xxx.rgb565 of=/sys/kernel/debug/qpic_display/image_dump bs=307200 count=1
# 或
echo /tmp/xxx.rgb565 > /sys/kernel/debug/qpic_display/image_dump
```

结论：

- 日志只有 `qpic_display_debugfs_image_dump_wr enter`，**没有**真正的 `qpic_panel_draw` / `generate_rgb565`。
- 屏幕上看不到自定义图案（只能先看到纯色 `draw`）。
- 向该节点写入大量数据有概率使 QPIC 异常，**导致整机重启**（已发生）。

因此：**不要用 `image_dump` 当写帧接口。**

### 方法 4：DRM Atomic KMS demo（已验证）

> **写操作，已在设备上执行**（部署二进制、停止 UI、点亮背光、Atomic commit）。

仓库 `files/qpic_demo/` 提供可交叉编译的小工具，路径与 `zte_topsw_devui` 一致：

```text
/dev/dri/card0
  → drmSetClientCap(ATOMIC)
  → CREATE_DUMB + ADDFB2（RGB565，双缓冲）
  → drmModeAtomicCommit（plane FB/CRTC/SRC/CRTC_* + crtc MODE_ID/ACTIVE + connector CRTC_ID）
  → qpic_display → 320×480 LCD
```

**不要**使用 legacy `drmModeSetCrtc` / `SETCRTC`（在设备上执行会导致 QPIC 超时重启）；**不要**使用 `image_dump`。

#### 文件

| 文件 | 说明 |
|---|---|
| `files/qpic_demo/qpic_drm_demo.c` | 源码：Atomic KMS + 内置 5×7 字体 |
| `files/qpic_demo/build.sh` | 交叉编译（默认 musl.cc + Alpine Docker） |
| `files/qpic_demo/run_qpic_demo.sh` | 停 UI → 开背光 → 跑 demo → 恢复 devui |
| `files/qpic_demo/README.md` | 构建/部署速查 |

#### 构建（主机）

设备上无 `gcc` / `modetest`，需在 PC 上 **aarch64 musl 静态交叉编译**：

```sh
cd files/qpic_demo
./build.sh
```

- **默认**：首次从 [musl.cc](https://musl.cc/) 下载 `aarch64-linux-musl-cross.tgz` 到本目录（gitignore，约 100MB），在 Alpine amd64 容器内编译。需 Docker；Apple Silicon 自动 `--platform linux/amd64`。
- **可选**：内网能拉 dockcross 时用 `DOCKCROSS=1 ./build.sh`（`dockcross/linux-arm64-musl`）。部分环境 harbor 镜像 401 会失败，故 musl.cc 为默认。
- 产物 `qpic_drm_demo` 已 gitignore；DRM uapi 头随工具链自带（Linux 5.15 布局，与设备内核匹配）。

#### 部署与运行（设备）

```sh
# 设备常无 scp，可用 base64 管道
base64 < qpic_drm_demo | ssh root@192.168.0.1 \
  'base64 -d > /data/qpic_demo/qpic_drm_demo && chmod +x /data/qpic_demo/qpic_drm_demo'
base64 < run_qpic_demo.sh | ssh root@192.168.0.1 \
  'base64 -d > /data/qpic_demo/run_qpic_demo.sh && chmod +x /data/qpic_demo/run_qpic_demo.sh'

ssh root@192.168.0.1 'sh /data/qpic_demo/run_qpic_demo.sh 12'
```

演示：红/绿/蓝 → 马赛克 `HELLO` → 时钟（电源键反色）→ 触摸轨迹（电源键退出）→ 清屏；日志 `/root/qpic_demo.log`。

最后一幕读 `/dev/input/event3`（Sitronix ST77921，MT-B），十字准星 + 轨迹，风格接近 Android 开发者选项 Pointer Location。须先停 `mtdev2tuio`（`run_qpic_demo.sh` 已做）。

#### 背光（必开）

LCD 背光由 AW9523B 驱动，节点 **`/sys/class/leds/led:lcd/brightness`**（非 `/sys/class/backlight/`）。停 devui 后亮度常为 **0**，DRM 帧在刷但屏幕全黑。

`run_qpic_demo.sh` 会在 demo 前 `echo 255 > .../brightness` 并退出时恢复原值。手动调试：

```sh
cat /sys/class/leds/led:lcd/brightness   # 0 = 看不见
echo 255 > /sys/class/leds/led:lcd/brightness
```

勿通过 `ubus call zwrt_bsp.led set` 控制 LCD（已在设备上调用，请求超时，且可能将亮度置回 0）；直接写 sysfs 即可。

#### IOCTL 注意（自研程序参考）

- `GETCONNECTOR` 首次探测须 `count_modes=1` 且 `modes_ptr` 指向临时 buffer；`count_modes=0` 无 master 时拿不到 mode。
- `GETRESOURCES` / `GETCONNECTOR` 第二次填充须为 encoder/props 指针分配内存，否则 `EFAULT`。
- Atomic 首次 commit 除 plane/crtc 外还须设 **connector `CRTC_ID`**，否则 `EINVAL`。

## 启动与服务

```sh
/etc/init.d/zte_topsw_devui start|stop|restart
ps | grep zte_topsw_devui
ubus call zwrt_deviceui zwrt_deviceui_touch_status_get
```

`/usr/ui/startdui.sh` / `startanimation.sh` 会 `killall -9 zte_topsw_devui`，动画结束后再由 init 或脚本启动 UI。

## 已知问题

1. **无 `/dev/fb0`**：无法向 `/dev/fb0` 写入帧缓冲；显示栈为 DRM + QPIC。
2. **`image_dump` 不是写帧接口**：会进入 write handler，但不刷新自定义图案，且可能导致重启。
3. **UI 独占 DRM**：未停止 `zte_topsw_devui` 时，其他程序提交的画面会被立即覆盖。
4. **触摸 I2C 偶发报错**：停止 UI 或调试显示时，dmesg 中可能出现 Sitronix `I2C ERROR TIMES`，与写屏测试穿插出现。
5. **设备重启后 ADB 短暂不可用**：与 USB debug 模式时序有关，见 [adb.md](adb.md)；主机侧可执行 `adb kill-server && adb start-server`。
6. **背光独立于 DRM**：Atomic commit 成功但屏幕全黑时，检查 `led:lcd/brightness` 是否为 0；devui 退出时通常关闭背光。
7. **legacy SETCRTC 会导致重启**：须使用 Atomic KMS（见 `files/qpic_demo/`）。

## 小结

| 目标 | 做法 | 备注 |
|---|---|---|
| 改 UI 文字 | 改 `/usr/ui/language/*.ini` + 重启 devui | 已验证 |
| 全屏纯色 | stop UI + `echo N > .../draw` | 已验证 |
| 自定义图案 / hello | `files/qpic_demo/` DRM Atomic demo | 已验证；须开背光 |
| 开机动画 | 换 `/usr/ui/anim/*.png` | 见 bootanim.md |

## 相关文档

- [bootanim.md](bootanim.md) — 开机动画（`/usr/ui/anim/`，320×480 PNG）
- [hardware.md](hardware.md) — 显示 / 触摸器件
- [ipc-protocol.md](ipc-protocol.md) — ZBUS 与 `zte_topsw_devui`
- [adb.md](adb.md) — 重启后 ADB 时序
- [README.md](../README.md) — 设备总览
