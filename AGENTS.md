# [AGENTS.md](http://AGENTS.md)

## 项目概述

`mu5250_tweaking` 是中兴 U60 Pro（MU5250，高通骁龙 X75 / SDXPINN 平台，aarch64）这台 **OpenWrt 定制版 5G 随身 WiFi** 的魔改/逆向分析记录。

- 目标设备：ZTE U60 Pro（MU5250）
- 平台 / SoC：Qualcomm Snapdragon X75（SDXPINN / SDX_PINNACLES，`soc_id=556`），4× Cortex-A53
- 系统：Linux 5.15.167-perf，OpenWrt 23.05.4 (sdx75)，`/` 为只读，需 overlay 挂到 `/data` 才能写
- 网络设备：默认网关 `192.168.0.1`，SSH root 密码 `admin123`
- 许可证：GPLv3（`LICENSE`）
- 主要魔改目标：开启 ADB/SSH、分区备份、安装 sing-box/ZeroTier、改开机动画、清空/改写 NFC 标签、屏幕定制、WiFi 开关控制等

> ⚠️ 本项目涉及设备逆向，仅限学习/安全研究/互操作目的，操作前请先按设备/分区做好备份（见 README「备份分区」），并对设备进行写操作前自行确认风险。

> 🔒 **隐私约束（重要）**：本项目为**公开项目**，仓库及其生成的分析文档/报告会被公开。任何涉及**个人/设备/卡片的标识信息**均须**模糊处理**后再写入：包括但不限于 **ICCID、IMSI、MSISDN/电话号码、IMEI、序列号、完整 MAC 地址、SIM 卡号**等。处理方式：可保留判定所需的**段前缀**（如 `MCC/MNC`、发行商号段），但**不得写入完整号码**；必要时用占位符/省略号（如 `ICCID 8985…`、`IMEI …xxxx`）代替。违反该约束的内容不得提交。



## 目录结构

```
.
├── README.md                 # 总览与上手：开 ADB、开 SSH、overlay 挂载、备份分区、安装软件
├── LICENSE                   # GPLv3
├── AGENTS.md                 # 本文件
├── TODO.md                   # 其它隐藏接口待探索清单（原 voip-slic.md 拆分的无关内容）
├── docs/                     # 各主题分析文档（见下方「文档索引」）
├── files/                    # 可直接部署到设备的实用脚本 / 工具
│   ├── enable_debugging.sh   # 网页端 ubus 登录 + zwrt_bsp.usb set mode=debug 开启 ADB
│   ├── trace_webui_ubus.sh   # WebUI action → ubus/daemon/脚本 追踪辅助（monitor/owner/map）
│   ├── init.sh               # 开机脚本：overlay 挂载只读目录 + 拉起 sshd/uhttpd/vsftpd/zerotier + 开调试
│   ├── nfc_set_ndef.sh       # 改写/备份/恢复 NFC 标签（FM11NT08, /dev/i2c-3, 0x57）NDEF
│   ├── guest_bssid.sh        # 修改 guest WLAN BSSID（UCI + wlan_mac.bin；关/开 guest VAP 重建生效，见 docs/wifi.md §八）
│   ├── sing-box              # sing-box 的 OpenWrt procd 服务脚本（/etc/init.d/sing-box）
│   └── qpic_demo/            # DRM/QPIC 屏幕 demo（C 源码 + build.sh 按需拉 musl.cc 工具链）
├── ref/
│   ├── datasheets/           # 器件数据手册（FM11NT082C pdf）
│   └── teardown/             # 充电头网拆解参考图（jpeg）
└── playground/               # 从设备提取的二进制（.gitignore 已排除，不入库）
    └── nfc_analysis/         # libzte_SDKowrt.so / libztedal.so / libztehalntag.so / libztephosal.so / zte_topsw_devui / zte_topsw_nfc
```



## 环境与构建约定（必读）

- **构建或测试优先使用 Docker**：涉及编译的 `files/qpic_demo` 运行 `./build.sh`（默认 musl.cc 工具链 + Alpine 容器；可选 `DOCKCROSS=1`，见 `files/qpic_demo/README.md` / `docs/screen.md`）。
- **若必须在本地运行，使用工具独立于环境的方式**（uv 创建环境、python venv 等），不要污染系统环境。
- 编译目标：aarch64-musl 静态二进制（目标设备是 OpenWrt musl）。



## 设备访问方式

```sh
# SSH 只读探测（设备在 192.168.0.x 局域网，需本地能路由到该网段）
ssh -o StrictHostKeyChecking=no root@192.168.0.1        # 密码 admin123
# 或
sshpass -p 'admin123' ssh root@192.168.0.1 '<cmd>'
```

- 沙箱网络默认不可达该设备，做只读探测通常需要提升权限执行。
- 只读探测常用节点：`/sys/devices/soc0/*`（socinfo）、`/sys/block/mmcblk0/device/*`（eMMC JEDEC）、`/sys/bus/i2c/devices/*`、`/sys/class/drm`、`/sys/class/power_supply`、`/proc/partitions`、`/proc/device-tree`、`/sys/bus/platform/devices/*/of_node/compatible`、`dmesg`。
- 涉及**写操作**（改 NFC、开机画面、WiFi 配置、`uci set wireless.zte_mbb.wifi_onoff` 等）的脚本/命令在执行前请仔细核对并先备份，文档中相关章节均已标注“写操作需自行确认”。



## 核心魔改流程（由 README + docs 归纳）

1. **开 ADB**：用 `files/enable_debugging.sh`（网页端 ubus 登录 `zwrt_bsp.usb set mode=debug`），或 MlgmXyysd 的 `openadb_MU5250` 脚本。
2. **开 SSH**：先把 `/` 等只读分区 overlay 挂到 `/data`（`files/init.sh`），再 `opkg update && opkg install openssh-server openssh-sftp-server vsftpd`，改 `/etc/ssh/sshd_config` 允许 root 登录。
3. **备份分区**：`dd if=/dev/mmcblk0 bs=4M > remote_disk.img`（69 个 user 分区，A/B 槽，见 `docs/partitions.md`）。
4. **持久化/开机自启**：`/etc/rc.local` 里 `(sleep 10; /bin/sh /data/init.sh > /tmp/mount.log 2>&1) &`。
5. **装软件**：定制内核导致 `opkg install` 常失败，需把 `.ipk` 解包、`data.tar.gz` 解压到根目录；注意删掉 `distfeed.conf` 里不适用的源（`openwrt_qti`*、`openwrt_zte_apps`、`core`）。
6. **各魔改主题**：见下「文档索引」。



## 文档索引（docs/）


| 文档                | 内容                                                                           |
| ----------------- | ---------------------------------------------------------------------------- |
| `partitions.md`   | 全 69 个分区 by-name→块设备→大小→挂载点映射（实测）                                            |
| `hardware.md`     | 硬件拆解配置（SoC/CPU/存储/内存/电池/屏幕/触摸/网络/外设），含 `ref/teardown/` 参考图                   |
| `adb.md`          | ADB 控制与启动机制（`adbd` + configfs gadget + `zwrt_bsp.usb`）                       |
| `screen.md`       | 屏幕控制（DRM+QPIC+`zte_topsw_devui`）、改语言文字、debugfs 画屏、已知问题                    |
| `bootanim.md`     | 改开机动画（`/usr/ui/anim/`，320×480 PNG）                                           |
| `nfc.md`          | NFC 碰一碰（`zte_topsw_nfc` + FM11NT08 标签）与 WLAN 开关关系                            |
| `wifi.md`         | WLAN 开关机制（`wifi_onoff` + `zte_topsw_wlan`）与 Guest BSSID 修改（`guest_bssid.sh`） |
| `smart-manage.md` | 智能管理（`zte_smart_manage` + xDPI 应用识别 / 家长管控 / Ai_mode QoS）                    |
| `usb-otg-host.md` | USB OTG/Host 实现（DWC3 双角色、Type-C DRP、usb2rj45）                                |
| `sing-box.md`     | sing-box 安装与 `files/sing-box` procd 脚本行为                                     |
| `zerotier.md`     | ZeroTier 手动安装（解包 ipk）与开机自启                                                   |
| `ipc-protocol.md` | zte_topsw_* 进程间通信（数据面 ubus + UI 层 ZBUS；SysV/shm/pipe 为辅助通道）                  |
| `webui-trace.md`  | WebUI action 追踪到 ubus/daemon/脚本（HTTP `/ubus/` + `ubus monitor`）              |
| `powerbank.md`    | 充电宝反向充电分析（PMIC PM7550B + SMB1394 + Type-C PD 反充）                             |
| `voip-slic.md`    | 语音 / FXS（VoIP）与音频子系统分析（SLIC 驱动休眠、SIM 语音根因厘清）                                 |
| `uci-config.md`   | UCI 配置全模块指南（设备上只读检查，约 71 个配置包分廿三节逐包脱敏整理）                                     |




## 文档行文

`docs/`、`README.md`、`TODO.md` 及本文件均用**正式书面汉语**。只写已确认的终态事实，不写探索过程。

- **结构**：开篇用简短「验证说明」交代依据（静态分析 / 设备上只读检查 / 是否执行过写操作）；正文按机制分节；文末列相关文档链接。不保留被后续结论覆盖的中间推断。
- **删减**：探索日记、日期流水、「本轮 / 重大进展 / 更正 / 待续」、易变快照（PID、临时 IP、ping、当时 SIM 状态）一律去掉。若某段探索备注仍有必要，缩成 1–2 句并保留 markdown 链接。
- **用词**：说「在设备上检查 / 执行」，不说「活机」「取证」。避免口语与生硬缩略（「走 ubus / 走一遍 / 不走 loopback」「私信中转」「自管 pipe」「三槽」「免重启重拨」）。技术名、路径、命令保持原文。
- **写操作**：凡会改变设备状态的步骤，标明「写操作」及是否已在设备上执行；执行前须备份并自行确认副作用。
- **隐私**：遵守上文「隐私约束」，标识信息须模糊后再写入。



## Git 约定

- 仓库已初始化，分支 `main`，提交信息简短（中文为主，如 `docs: ...`、`readme: ...`、`add ...`）。
- `playground/` 与 `.DS_Store` 已在 `.gitignore` 排除，不入库。
- 修改文件后按逻辑分组提交；新增大块二进制/图片需确认是否入库（参考此前「把设备分析二进制移到 gitignored 的 playground」的做法）。
- 所有做**静态或动态分析**用到的文件（从设备提取/拉取的二进制、镜像、固件、日志等）一律先放入 `playground/` 下再分析；`playground/` 已在 `.gitignore` 中排除，如此分析产物不会污染仓库、无需逐文件判断是否入库。

