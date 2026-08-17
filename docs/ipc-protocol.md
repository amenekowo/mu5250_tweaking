# zte_topsw_* 进程间通信

> **验证说明**：本文基于对 `zte_topsw_*` 守护进程的静态分析，以及在设备上的只读检查。分析产物位于 `playground/ipc_analysis/`（已列入 `.gitignore`，不纳入版本库）。`zwrt_mdm_notify_sim_state` 等会改变设备状态的 ubus 方法**尚未在设备上调用**；写操作须先行备份并自行确认副作用。

数据面的 SIM 就绪与拨号事件通过 **ubus notify/subscribe** 传递；触摸屏 UI 使用 **ZBUS**（Unix domain socket + JSON）。System V 消息队列、共享内存与进程内 pipe 为辅助通道，不承载 SIM 就绪主链路。

## 一、双层模型

`zte_topsw_data` / `zte_topsw_mdm` / `zte_topsw_mc` / `zte_topsw_nwinfo` 的动态依赖含 `libubus` / `libubox` / `libblobmsg_json`，**不链接** `libzte_zbus`。`libzte_zbus-2.0.0.so` 供 UI 客户端（`zte_topsw_devui` / `zte_topsw_nfc`，经 `libzte_SDKowrt.so`）查询与订阅状态。

```
数据面     zte_topsw_mdm --ubus notify--> zte_topsw_data --QMI/DSI--> modem
           （zwrt_zte_mdm.api）            subscribe
UI         zte_topsw_devui --libzte_SDKowrt.so--> libzte_zbus-2.0.0.so
           （Unix socket `/tmp/run/ubus/ztesock_%u` + ubus 订阅）
```

相关进程：

| 程序 | 作用 | 启动 |
|---|---|---|
| `/usr/bin/zte_topsw_data` | 数据拨号 | procd，START=45 |
| `/usr/bin/zte_topsw_apn` | APN 管理 | procd，START=44 |
| `/usr/bin/zte_topsw_mdm` | modem / SIM | procd |
| `/usr/bin/zte_topsw_nwinfo` | 网络注册信息 | procd |
| `/usr/bin/zte_topsw_mc` | 模块注册与消息转发 | procd |
| `/usr/bin/zte_topsw_daemon` | 总控 | procd |
| `/usr/bin/QCMAP_ConnectionManager` | 高通连接管理（实际拨号引擎） | ujail(radio) |

## 二、数据面：SIM 就绪到自动拨号

`zte_topsw_mdm`（ubus 对象 `zwrt_zte_mdm.api`）在 SIM 就绪后调用 `zte_mdm_ubus_notify_sim_status_ready`，对方法 `zwrt_mdm_notify_sim_state` 做 `ubus_notify`。

`zte_topsw_data` 订阅该通知后解析 `sim_states`，设置内部标志 `zte_data_sim_ready`，再经 `zte_data_auto_connect_process_for_one_apn` 核对 APN（`zwrt_zte_mdm.sim_info.mdm_mcc/mnc/spn_name_data/sim_active_card_id` 与本地 APN 表），调用 `dsi_start_data_call` 建立承载。`sim_ready=0` 或 APN 数组为空时拒绝自动连接。该链路仅在开机 SIM 检测时触发一次。

在设备上执行 `ubus -v list zwrt_zte_mdm.api`，可见 `"zwrt_mdm_notify_sim_state":{"sim_card_id":"Integer"}`。

> **不重启而重新拨号（写操作，尚未执行）**：对 `zwrt_zte_mdm.api` notify/call `zwrt_mdm_notify_sim_state {"sim_card_id": <n>}`，可使 `zte_topsw_data` 重新评估 SIM/APN 并尝试 `dsi_start_data_call`。该操作会触发拨号或断线重拨，执行前须备份并确认副作用。

## 三、相关 ubus 对象

| 对象 | 关键方法 | 归属 |
|---|---|---|
| `zwrt_zte_mdm.api` | `zwrt_mdm_notify_sim_state {sim_card_id}`、`zwrt_mdm_activate_sim`、`get_sim_info`、`sim_get_slot` | `zte_topsw_mdm` |
| `zwrt_data` | `get/set_wwaniface`、`set_wwanapn`、`get_wwandst` | `zte_topsw_data` |
| `zwrt_apn_object` | `get_manu_apn_list`、`add_manu_apn`、`enable_manu_apn_id`、`set_apn_at_cid` | `zte_topsw_apn` |
| `zwrt_router.api` | `router_get_status`、`router_wan_status_event` | 路由器 / WAN |
| `zte_nwinfo_api` | `nwinfo_get_netinfo`、`nwinfo_set_mode`、`nwinfo_manual_register` | `zte_topsw_nwinfo` |
| `zwrt_qcmap_cli` | `get_qcliwwanstatus`、`get_v4addr`/`get_v6addr`、`set_qcliiface` | QCMAP CLI |
| `zwrt_zte_mc` / `zwrt_zte_mc_tmp` | 模块注册、电源/FOTA 联动 | `zte_topsw_mc` |

`libzte_SDKowrt.so` 还可枚举 `zwrt_bsp.{battery,charger,led,powerbank,rtc,typec,usb}`、`zwrt_datausage.info.*`、`zwrt_deviceui_event.{lcdstatus,touchstatus}`、`zwrt_topsw_daemon.sync`、`zwrt_zte_dm.dm_update.*`、`zwrt_key.event`、`zwrt_smart_mng.api`、`zwrt_fota_res.api` 等。UCI 侧对应配置见 [uci-config.md](uci-config.md)。

跨守护进程 ubus 事件名：

| 事件名 | 归属 |
|---|---|
| `ZTE_DATA_UBUS_EVENT_SIM_STATES` | `zte_topsw_data` 接收（拨号触发链） |
| `ZTE_NWINFO_UBUS_EVENT_REG_DOMAIN` | `zte_topsw_data` 引用 |
| `ZTE_NWINFO_UBUS_EVENT_ROAM_STATUS` | `zte_topsw_data` 引用 |
| `ZTE_NWINFO_UBUS_EVENT_SERVICE_STATUS` | `zte_topsw_data` 引用 |
| `ZTE_NWINFO_UBUS_EVENT_CARD_STATE` | `zte_topsw_nwinfo` |
| `UBUS_EVENT_DEVICE_POWER_STATE` | `zte_topsw_mdm` |

## 四、消息中心 `zte_topsw_mc`

`zte_topsw_mc` 负责各 `zte_topsw_*` 之间的模块名 ↔ ID 映射与消息转发。`moduleMap`（`initModuleMap` / `addModuleMapFlag` / `deleteModuleMapFlag`）按序注册 17 个 daemon/agent：

`zte_topsw_tr069`、`zte_trans_agent`、`zte_topsw_mdm`、`zte_topsw_wms`、`zte_router`、`zte_ripc`、`zte_topsw_daemon`、`zte_monitor_daemon`、`zte_topsw_atfwd`、`zte_cutoff_product`、`zte_net_link_detect`、`zte_topsw_data`、`zte_topsw_nwinfo`、`zte_topsw_devui`、`zte_topsw_sleep_faw`、`zte_topsw_diag`、`zte_production_server_at`。

mc 使用自建 `pipe()` 传递 JSON：`{cmd:整数, para:JSON}`（日志 `mc_pipe_send_msg: start write data cmd=%d, para=%s`）。跨进程消息经 mc 转发后立即消费，不在 SysV 队列中滞留。

## 五、辅助通道

### 5.1 System V 消息队列

`zte_topsw_data` / `zte_topsw_mdm` 含 `msgget` / `msgsnd` / `msgrcv`。消息头为 8 字节小端：`{u16 TargetModuleID, u16 SourceModuleID, u16 MsgCmd, u16 DataLen}` + 载荷（日志 `ParseMsgQ`）。

设备上 `/proc/sysvipc/msg` 的 key 成对出现（`0x6119xxxx` / `0x6219xxxx`），推测每个模块各有一对收发队列。多次轮询（含重启 `zte_topsw_apn` / `zte_topsw_data`）时 **qnum 恒为 0**；以 `MSG_COPY` 只读查看亦未捕获到消息。这些队列不是 SIM 就绪的主通道，可能仅用于轻量确认或同步。loopback 上几乎没有 TCP/UDP 流量，守护进程之间不通过 loopback 通信。

### 5.2 共享内存 `key=5139`

`/proc/sysvipc/shm`：`key=5139`，64 字节，开机由 `zte_ubus_bsp_usb` 创建，约 44 个进程 attach（含各 `zte_topsw_*`、`sbusd`、`QCMAP_CLI`、`dnsmasq`）。多次采样内容不变：offset 0 为 u64 `0x00007fffffffffff`，offset 0x10 为 u32 `1`，其余为 0。`zte_topsw_data/mdm/mc/nwinfo` 及 `libzte_SDKowrt.so` 均不 import `shmget/shmat/shmctl`；attach 来自底层 sbus 相关库。与数据面 IPC 无关。

### 5.3 进程内 pipe

`zte_data_ubus_pipe_uloop_fd_*` 等 `ZTE_DATA_PIPE_*` / `ZTE_MDM_PIPE_*` 为**进程内部** uloop 管道，不是跨进程主通道（各 daemon 为独立 procd 实例，不继承管道 fd）。`/tmp/run/ubus/ztesock_<pid>` 为 ZBUS 的 Unix domain socket（§六），另有 `/tmp/zte_apn_tmp`、`/tmp/MSG_ID_TR069`、`/dev/socket/data/qcmap_*` 等专用 socket。`/data/data_tlpd/tlpd.log` 属于 Spearhead/QMI 诊断日志，与守护进程主 IPC 无关。

`zte_topsw_data` 内部子模块：`nwinfo` / `deamon` / `router` / `sim` / `mc` / `apn`。`ZTE_DATA_PIPE_*` 命令：

`CONNECT_STATUS`、`CONNECT_SUCCESS_APN_PROFILEID`、`USING_APN_PROFILEID`、`QCMAP_WAN_ACTION`、`QCMAP_GET_V4_ADDR`、`QCMAP_GET_V6_ADDR`、`QC_SET_CFUN`、`STOP_CONNECT_SYNC`、`STOP_CONNECT_ASYNC`、`CHECK_IF_AUTO_OR_ROLL_CONNECT`、`REBOOT_DEVICE`、`CONNECT_STATUS_CONNECTING`、`CONNECT_STATUS_IPV4_CONNECTED`、`CONNECT_STATUS_IPV6_CONNECTED`、`CONNECT_STATUS_IPV4_IPV6_CONNECTED`、`CONNECT_STATUS_DISCONNECTING`、`CONNECT_STATUS_DISCONNECTED`、`POWEROFF_COMPLETE`。

底层拨号由 DSI（`dsi_start_data_call` / `dsi_stop_data_call` / `dsi_set_data_call_param` / `dsi_get_ip_addr`）与 QMI WDS（`wds_get_service_object_internal_v01`）发起。

## 六、ZBUS（UI 客户端层）

依赖链：`zte_topsw_devui` → `libzte_SDKowrt.so`（`src/sdk_zbusmanager.c`）→ `libzte_zbus-2.0.0.so`。

传输层分两路：

- **Unix socket**：`src/libzte_usock_server.c`，每个 uid 对应一个 `/tmp/run/ubus/ztesock_%u`，收发 JSON 信封（`zbustype`、`zbuspeer`、`value`、`valueSize`、`errorCode`、`zbusAction`、`peer`）。SDK 以 cJSON 序列化，日志 `sendZbusObjMessage before buff:%s` 打印明文，未见加密（tlpd 中的 `No aes key found` 属于另一条诊断通道）。
- **ubus**：`src/libzte_ubus_client.c`，`ubus_subscribe` / `notify` / `send_event`；表 `zTopicTable` / `eventTable` / `invokeTable`。

导出 API 包括 `zBusInit`、`zBusSendMsg`、`zBusRegisterCallBack`、`runSDKzwrtclient`、`run_sdkzwrt_server`、`ubus_set_table`、`zbus_notify`、`zbus_send_reply`。RPC 为请求-应答：客户端 `zBusSendMsg` 后阻塞等待信号或超时（`… recv signal` / `… time out`）。

`ubus_set_table(table, count)` 遍历 `{u32 type, pad, u64 ptr}`（每项 0x10 字节），按 type 写入 BSS 中的三个表槽：`type=1 → +0x1b0`、`type=2 → +0x260`、`type=3 → +0x1a0`（对应 `zTopicTable` / `zEventTable` / `zInvokeTable`，条目内容由 SDK/APP 传入）。

### 6.1 `ZBUS_TYPE_*` 枚举

| ZBUS_TYPE_* | 方向 | 载荷键 |
|---|---|---|
| `LISTEN_SIMCARD_STATE` | 订阅 | `sim_states`、`sim_hotswap`、`sim_pin_puk_wait` |
| `LISTEN_DATA_CONNECT_STATUS` | 订阅 | `connect` |
| `LISTEN_ROUTER_WAN_STATUS` | 订阅 | `connect` |
| `LISTEN_SMS_STATE` | 订阅 | `wms_status`、`new_sms_received` |
| `LISTEN_BSP_CHARGER_EVENT` | 订阅 | `charge_status`、`charger_connect` |
| `LISTEN_BSP_KEY_EVENT` | 订阅 | `keyName`、`keyAction` |
| `LISTEN_BSP_USB_EVENT` | 订阅 | `connect` |
| `LISTEN_BSP_RTC_ALARM` | 订阅 | `status` |
| `LISTEN_MC_RUN_MSG` | 订阅 | `processstatus`、`RUN` |
| `LISTEN_MC_DEVICE_POWER_STATE_MSG` | 订阅 | `action`、`para` |
| `LISTEN_MC_SLEEP_STATUS` | 订阅 | `action`（`gotoSleep` / `gotoWakeup`） |
| `LISTEN_FOTA_UI_MSG` / `FOTA_UPDATE_RESULT_MSG` | 订阅 | `reserved`、`type` |
| `LISTEN_NFC_STATE_CHANGE` | 订阅 | NFC 状态 |
| `LISTEN_WEB_LOGIN_EVENT` | 订阅 | `login_ubus_rpc_session` |
| `LISTEN_WIFI_MSG` | 订阅 | wifi / wps |
| `LISTEN_SLEEP_REREGISTRATION_EVENT` | 订阅 | — |
| `BSP_GET_USB_STATE` / `GET_POWERBANK_STATE` / `GET_TYPEC_ROLE` | RPC | 状态查询 |
| `SMS_GET_SMS_STAUS` | RPC | — |
| `WEB_GET_WEBINFO` | RPC | — |
| `REPLY_DEVICEUI_DIRECT_POWER_MODE_SET` | 应答 | `zwrt_deviceui.Device.direct_power_mode_switch` |
| `SUB_TOPIC_MC_SLEEP_STATUS` | 订阅 | — |

SDK 解析的状态键覆盖 `sim_info`（`zwrt_zte_mdm.sim_info.*`、`current_sim_slot`、`sim_hotswap` 等）、注册/WAN（`zte_sdk_parse_net_register_status`、`current_wan_status`）与数据（`zwrt_data_connect_status`、`ZTD_GetWanConnectInfo`）。`__zte_sim_state_str_to_enum` 将字符串状态转为内部枚举。UI 层订阅 `LISTEN_SIMCARD_STATE`；数据面 SIM-ready 仍走 §二的 ubus notify，不经 ZBUS。

## 七、分析工具

均位于 `playground/ipc_analysis/`（已列入 `.gitignore`）。设备上无 `gcc` / `strace` / `gdb` / `ipcs` / `timeout`；`ubus listen` 与 `logread` 容易阻塞，宜改用 `ubus monitor`（见 [webui-trace.md](webui-trace.md)）。

| 文件 | 说明 |
|---|---|
| `bin/` | 自设备提取的 `libzte_zbus-2.0.0.so`、`zte_topsw_data` / `mdm` / `mc` / `nwinfo` 等 |
| `msgq_sniffer.c` | 以 `MSG_COPY` 只读查看 SysV 消息队列，解析 `ParseMsgQ` 头 |
| `shm_dumper.go` | 只读转储 shm `key=5139`（`CGO_ENABLED=0 GOOS=linux GOARCH=arm64`；syscall 号 `SYS_SHMGET=194` / `SHMAT=196` / `SHMDT=197`） |
| `disasm_lib.py` | 在独立 Python 虚拟环境中（capstone + pyelftools）做 aarch64 反汇编与 ELF 解析 |

设备端：`/tmp/msgq_sniffer [interval_sec] [max_msgs_per_queue]`。静态分析可用 `readelf -d`、`objdump -T`、`llvm-objdump -d`、`strings -a`。

## 八、未决事项

- 细分 `ubus_set_table` 三个表槽（type=1/2/3）与 `zTopicTable` / `zEventTable` / `zInvokeTable` 的对应关系，并从 SDK/APP 调用点恢复表项。
- 若需给出命令字的数值定义，再反推 SysV 消息头中的 module ID / msgCmd 枚举（当前以字符串名为准）。

## 相关文档

- [uci-config.md](uci-config.md) — 拨号/APN 等 UCI 配置
- [webui-trace.md](webui-trace.md) — WebUI → HTTP-ubus 追踪（与 ZBUS UI 并行）
- [wifi.md](wifi.md) — WLAN 开关（同属 `zte_topsw_*` 体系）
- [smart-manage.md](smart-manage.md) — `zwrt_smart_mng.api`
- [nfc.md](nfc.md) — `zte_topsw_nfc` 与 `libzte_*`
- [README.md](../README.md) — 设备总览
