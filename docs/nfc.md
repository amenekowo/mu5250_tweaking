# NFC 碰一碰（WiFi 快速分享）

> **验证说明**：本文基于对 `zte_topsw_nfc` 及 `libztehalntag.so` / `libztephosal.so` / `libztedal.so` 的静态分析，以及在设备上的只读检查（UCI、`i2cdetect` / `i2ctransfer` 读、GPIO、dmesg、`ubus call zwrt_nfc zwrt_nfc_wifi_get`）。分析产物位于 `playground/nfc_analysis/`（已列入 `.gitignore`，不纳入版本库）。`files/nfc_set_ndef.sh`、直接 `i2ctransfer` 写入，以及 `uci set` 修改 `zwrt_zte_nfc` / `wireless.zte_mbb.wifi_onoff` 等写操作**尚未在设备上执行**；写操作须先行备份并自行确认副作用。

U60 Pro 的「NFC 碰一碰」由 `zte_topsw_nfc` 实现：设备内嵌 NFC 标签芯片，守护进程经 I2C 将 WiFi 凭证写入标签，手机贴近后读取并连接。

## 一、相关程序

| 文件 | 说明 |
|---|---|
| `/usr/bin/zte_topsw_nfc` | NFC 守护进程，procd 启动，参数 `EN`（EN 模式） |
| `/etc/init.d/zte_topsw_nfc` | 启动脚本（`procd_set_param command "$PROG" EN`） |
| `/etc/config/zwrt_zte_nfc` | 配置：WiFi 凭证与开关 |
| `/usr/lib/libztehalntag.so` | NXP NTAG I2C Plus 官方驱动（`NTAG_ReadBytes/WriteBytes` 等全套 API） |
| `/usr/lib/libztephosal.so` | NXP OSAL 移植层（phOsal_Mutex/Sem/Event/Thread/Timer） |
| `/usr/lib/libztedal.so` | NXP PH DAL 移植层（`phbalReg_*` I2C 总线访问 + `phDriver_Pin*`/`PiGpio_*` GPIO 访问） |

## 二、硬件接口

芯片为**复旦微 FM11NT08**（NXP NTAG I2C Plus 的国产兼容替代）。守护进程内嵌 `src/NFC_HAL/hal_adapter/fm11_tag/fm11_ee.c` 与 `src/NFC_Biz/zte_wifi_token_RW.c`（由二进制字符串还原）。

- I2C 总线：`/dev/i2c-3`（QCOM GENI I2C，硬编码），从机地址 **0x57**
- GPIO 482 = FD 场检测（输入，`edge=falling`），手机靠近时跳变，用于触发写标签
- GPIO 498 = CSN 芯片使能/复位（输出）
- IRQ 经 `/dev/irq_event0` 上报，用于手机向标签写入时通知守护进程

在设备上执行 `i2cdetect`：总线上仅有 0x57。寄存器区 `NS_REG=0x03`、`I2C_CLOCK_STRETCH=0x14`。FM11 寄存器访问使用 2 字节大端地址（如 `0x3b0` = SAK 控制），按 16 字节一页写入。

## 三、工作原理

- **碰一碰连接 WiFi（写）**：守护进程轮询 FD 引脚（`ppoll` 等边沿），检测到手机贴近后，将 UCI 中的 WiFi 凭证（SSID / 密码 / 认证 / 加密方式）拼成 NDEF 记录写入标签内存，手机读取后自动连接。
- **反向导入（读）**：手机向标签写入并触发 IRQ，守护进程读回 NDEF，解析后经 `uci_set` 写入 `zwrt_zte_nfc`。
- 事件经 zBus 分发（含 `zte_nfc_test` 测试接口）；mesh 组网信息经 ieee1905 同步（`CAP_SYNC_RE_NFC_INFO`）。

标签用户内存中的 WiFi 记录为标准 **WiFi Simple Configuration** NDEF（MIME 类型 `application/vnd.wfa.wsc`），见 §六。

## 四、配置

```sh
cat /etc/config/zwrt_zte_nfc
```
```text
config nfc 'nfc_info'
	option switch '1'              # NFC 功能总开关
	option flag '2'
	option nfc_read_flag '0'       # 允许从标签读取（手机→路由器导入）
	option nfc_write_flag '0'      # 允许写入标签（路由器→手机）
	option nfc_wifi_ssid '<SSID>'
	option nfc_wifi_auth_type '32'
	option nfc_wifi_encry_type '8'
	option nfc_wifi_key '<WiFi密码>'
```

## 五、改写标签（写操作，尚未执行）

- 修改 `nfc_wifi_ssid` / `nfc_wifi_key` 即改变碰一碰写入的 WiFi 信息；改完后须 `/etc/init.d/zte_topsw_nfc restart`。属写操作，须先行备份并自行确认副作用。
- 关闭 NFC：将 `switch` 改为 `0`。属写操作。
- **将标签改为任意 URI**（例如网页）：使用仓库中的 `files/nfc_set_ndef.sh`（在设备上运行），例如：

  ```sh
  sh nfc_set_ndef.sh                        # 写 http://www.bing.com
  sh nfc_set_ndef.sh www.bing.com 0         # 原样写 "www.bing.com"
  sh nfc_set_ndef.sh example.com 5          # https://example.com
  ```

  脚本会先停止守护进程（否则手机贴近时，守护进程会把 NDEF 改回 WiFi 凭证），自动备份原内容到 `/tmp/nfc_tag_backup_*.hex`，然后按 FM11 的 2 字节大端地址 + 16 字节页协议写入。
- **备份 / 恢复**：`nfc_set_ndef.sh backup` 只备份、不改动；`nfc_set_ndef.sh restore <备份文件>` 还原。若恢复原 WiFi 分享 NDEF 后需重新启用碰一碰，执行 `/etc/init.d/zte_topsw_nfc start`。
- 亦可直接操作 I2C：
  `i2ctransfer -y 3 w18@0x57 <addr_hi> <addr_lo> <16字节数据>`（NDEF TLV 在 0x10，CC 在 0x0c）。

> ⚠️ **写操作提醒**：以上均会改写标签或 UCI，**尚未在设备上执行**。I2C 写入按 16 字节页回绕，地址必须页对齐（0x10 起）；I2C 控制器写后约有 50% 概率报 `ENOTCONN`，写完须间隔 50ms 以上并在失败时重试（脚本已处理）。

## 六、与 WLAN 开关的关系

关闭 WLAN **不会**禁用 NFC 硬件或守护进程。NFC 芯片与 `zte_topsw_nfc` 在 WLAN 关断后仍正常工作。

WLAN 总开关不在 `zwrt_zte_nfc`，而在 `/etc/config/wireless` 的 `feature 'zte_mbb'` 段：

```sh
uci show wireless.zte_mbb
# wireless.zte_mbb.wifi_onoff='0'          # WLAN 总开关（1=开，0=关）
# wireless.zte_mbb.wifi_onoff_by_user='0'  # 是否用户手动操作
```

关闭 WLAN 时的内核 / 服务日志（dmesg）：

```
zteqcawifi 1037: handle_wifi_onoff 0        # 触发 WLAN 关闭
hostapd-daemon: stop wlan0 ifdown
hostapd-daemon: stop wlan2 ifdown
```

关闭后 `br-lan/brif` 中只剩 `eth0`，`wlan0/1/2/3` 网络接口全部消失。

WLAN 关闭后的 NFC 只读检查：

| 检查项 | 结果 |
|---|---|
| NFC daemon 进程 | `/usr/bin/zte_topsw_nfc EN` 仍在运行 |
| ubus 开关 | `zwrt_nfc zwrt_nfc_wifi_get` → `switch:1, flag:2`（启用） |
| UCI NFC 配置 | `zwrt_zte_nfc.nfc_info.switch='1'` |
| NFC 芯片 I2C 可达 | 强制读 `0x57` NS_REG = `0x69 0x42`，正常应答 |
| GPIO 498（CSN 使能） | out = 0（低有效使能态） |
| GPIO 482（FD 场检测） | in = 1（常态） |
| 标签内容 | 0x10 起为 `application/vnd.wfa.wsc` NDEF，内含 SSID 与 key 的 WiFi 配置，与 `zwrt_nfc_wifi_get` 一致 |

用户内存 0x10~0xff（SSID / 密码已隐去）：

```
0x10: 03 53 d2 17 39 61 70 70 6c 69 63 61 74 69 6f 6e   # TLV+NDEF头, type="application"
0x20: 2f 76 6e 64 2e 77 66 61 2e 77 73 63 10 0e 00 35   # "/vnd.wfa.wsc" WSC 记录
...（用户内存区，含当前 SSID / 密码，内容已隐去）...
```

即芯片内存为格式正确的 WiFi Simple Configuration（`application/vnd.wfa.wsc`）NDEF。

`zte_topsw_nfc` 会监听 WLAN 开关，但只做 WiFi 索引联动，不禁用芯片。strings 可见：

```
zte_topsw_nfc: %s : wifi_onoff is %s, wifi_lbd_old is %s, wifi_lbd_new is %s, guest_enable_old is %s, guset_enable is %s
zte_topsw_nfc: guest disable
zte_topsw_nfc: set wifi index, nfc_switch:%d, wifi_index:%d
zte_topsw_nfc: switch state change:%s
```

WLAN 开关或 guest 状态变化时，守护进程会联动刷新碰一碰所写入的 WiFi 索引（将当前 SSID / 密码同步到标签写入逻辑），与禁用 NFC 芯片无关。关闭 WLAN 不会将芯片断电或停止守护进程。

若要恢复「碰一碰连接 WiFi」：

- WLAN 若关闭（`wifi_onoff=0`），即便标签内容正确，手机也无法关联——须先恢复 WLAN：`uci set wireless.zte_mbb.wifi_onoff=1 && uci commit wireless` 并重启对应 hostapd / wifi 服务。该命令属写操作，**尚未在设备上执行**，执行前须备份并确认副作用。
- NFC 标签本身即为正确的 WiFi NDEF，无需重写；若曾写入自定义 URI，按上文 `nfc_set_ndef.sh restore` 恢复即可。

## 相关文档

- [wifi.md](wifi.md) — WLAN 总开关 `wifi_onoff` 与 `zte_topsw_wlan`
- [ipc-protocol.md](ipc-protocol.md) — zBus / `zte_topsw_*` 进程间通信
- [uci-config.md](uci-config.md) — `zwrt_zte_nfc` 等 UCI 配置
- [hardware.md](hardware.md) — NFC 芯片与 I2C 总线
- [README.md](../README.md) — 设备总览
