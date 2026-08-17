# 充电宝（反向充电）

> **验证说明**：本文基于对 BSP 守护进程与 SDK 的静态分析（`strings`），以及在设备上对 sysfs / ubus / UI 语言串的只读检查。`zwrt_bsp.powerbank set`、直接写 `powerbank_zte` sysfs、`zwrt_bsp.typec set` 等会改变设备状态的操作**尚未在设备上执行**。写操作须先行备份并自行确认副作用。

充电宝反向充电为 U60 Pro 零售宣传并实装的功能：将内置 10000mAh 电池经 USB-C 口向手机等设备供电。固件自底层驱动、BSP ubus、SDK 至触摸屏 UI（「充电宝模式」）均有对应实现。与 [voip-slic.md](voip-slic.md) 所述「固件具备能力但硬件未连接」的接口不同。

## 一、调用链（UI → 硬件）

- **触摸屏 UI**：主界面有「**充电宝模式**」开关（`TUFormSupport_LabelText_PowerBank`）。
- **充电状态动画**：`TUFormBootAnimation_ChargingTip_Powerbank="对外充电中"`。
- **保护提示**：低电量 / 高温时禁用的弹窗文案。

```
触摸屏 UI (zte_topsw_devui, LVGL)
   │  ZTD_SetPowerBankEnableState() / ZTD_GetPowerBankState()
   ▼
libzte_SDKowrt.so  (zte_sdk_parse_powerbank_info, ZBUS 消息)
   │  ZBUS_TYPE_LISTEN_BSP_POWERBANK_EVENT
   ▼
zte_ubus_bsp_pm  (ubus: zwrt_bsp.powerbank)
   │  zte_bsp_set_powerbank_enable()
   ▼
/sys/class/power/zte_power/powerbank_zte/*   +   /sys/class/typec/port0/power_role
   ▼
PMIC PM7550B + SMB1394 电荷泵 + Type-C PD → USB-C VBUS 反向输出
```

## 二、BSP ubus 入口

充电宝相关 ubus 对象 `zwrt_bsp.powerbank` 由 `/usr/bin/zte_ubus_bsp_pm` 提供（`strings` 确认）。

### `zwrt_bsp.powerbank`

- `get`：参数 `property`。可取属性（对应内核 sysfs）：
  - `online_mbb`　　→ `powerbank_zte/online_mbb`（反充使能/在线位）
  - `device_attached`→ `powerbank_zte/attached`（`Attached`/`Unattached`）
  - 协议　　　　　　→ `powerbank_zte/protocol`
- `set`：`zwrt_bsp.powerbank set {"state": 1|0}` → 内部 `zte_bsp_set_powerbank_enable`（`enable`/`disable`）。

在设备上检查（未接入外部设备）：

```text
attached    = Unattached
protocol    = Unknown
online_mbb  = 0
```

## 三、内核 sysfs 状态源（`/sys/class/power/zte_power/powerbank_zte/`）

| 节点 | 含义 | 实测值 |
|---|---|---|
| `online_mbb` | 反充使能/在线位 | `0`（关） |
| `attached` | 是否有设备插入（Attached/Unattached） | `Unattached` |
| `protocol` | 反向充电协议 | `Unknown` |
| `present_mbb` | 电池在位 | `0` |
| `pb_state` | 芯片使能 / PM7550 DPM CC 状态 | `chip en state 0x00`… |
| `force_always_on` | 强制常开位 | `0` |
| `policy` | **反充输出限流/限压策略表**（见下） | （大表） |

Type-C 侧：`/sys/class/typec/port0/power_role` 应为 `source`（对外供电）。在设备上检查（未反充）：`source [sink]`，即当前是 `sink`（在充电）。

### `policy`：反充输出保护策略（内核实测 dump）

对 **SoC 电量 / 电池温度 / 电池电流 / 环境温度** 4 路输入查表，决定**输出限流 `ilim` 与限压 `vlim`**：

```text
******soc_ctrl_ilim******        SoC→限流
  0-10%      → 0 mA           10%以下禁反充
 11-30%      → 1000 mA
 31-100%     → -1 (不限)
******soc_ctrl_vlim******        SoC→限压
  0-10%      → 0 mV
 11-30%      → 5000 mV
 31-100%     → -1
******temp_ctrl_ilim******       电池温度→限流
  -50..45°C  → -1   46..51 → -1  52..53 → 1000  54..57 → 1000  58..60 → 0
******temp_ctrl_vlim******       电池温度→限压
  46..51 → -1  52..53 → 9000  54..57 → 5000  58..60 → 0
******bat_current_ctrl_*  ambient_ctrl_* ...   （电池电流 / 环境温度查表）
```

- 工作温度区间：`min_work_temp_threshold=0`, `max_work_temp_threshold=58`。
- 当前快照：`soc=100, bat_temp=36, bat_current=0, ambient=0`。

反充输出受控：电量 ≤10%、温度 ≤0°C 或 ≥58°C 时禁止输出；中低温 / 中高电量按档位限流限压。此为电池健康保护。

## 四、触摸屏 UI 与语言串

`/usr/ui/language/*.ini` 关键串（多语言齐全，中文/英文/阿拉伯等）：

| key | Chinese | English |
|---|---|---|
| `TUFormSupport_LabelText_PowerBank` | 充电宝模式 | Power Bank Mode |
| `TUFormBootAnimation_ChargingTip_Powerbank` | 对外充电中 | Power Bank |
| `TUForm_MSG_PowerBank_LowBattery` | 电池电量低，充电宝不可用 | Battery is low, powerbank is disabled |
| `TUForm_MSG_PowerBank_HighTemp` | 电池温度过高，充电宝不可用 | Battery temperature is too high… |

SDK 内部函数（`libzte_SDKowrt.so`）：

```text
ZTD_SetPowerBankEnableState / ZTD_GetPowerBankEnableState / ZTD_GetPowerBankState
ZTD_GetPowerBankProtocal / ZTD_GetPowerBankDeviceAttachStatus
zte_sdk_parse_powerbank_info
ZBUS_TYPE_LISTEN_BSP_POWERBANK_EVENT
```

## 五、开启方式

> ⚠️ 以下为**写操作**，**均未在设备上执行**。执行前须确认：电池电量充足、设备已备份，并明确副作用（会将电池电量供给外部设备）。

**方式 A：触摸屏（官方路径）**

主界面 → 「充电宝模式」开关打开，USB-C 口接手机即可对外充电。此为零售用户的正规操作。

**方式 B：BSP ubus（命令行）**

```sh
# 查询当前状态（只读）
ubus call zwrt_bsp.powerbank get '{"property":"online_mbb"}'

# 使能充电宝（反充）——写操作
ubus call zwrt_bsp.powerbank set '{"state":1}'

# 关闭反充
ubus call zwrt_bsp.powerbank set '{"state":0}'
```

**方式 C：直接写 sysfs（最底层，须谨慎）**

```sh
# 读取策略表确认当前允许输出（只读）
cat /sys/class/power/zte_power/powerbank_zte/policy

# 使能（写）——危险，谨慎
echo 1 > /sys/class/power/zte_power/powerbank_zte/online_mbb
echo 1 > /sys/class/power/zte_power/powerbank_zte/force_always_on
```

反充前若当前 `power_role=sink`（在充电），通常还需先把 Type-C 电源角色切到 `source`：

```sh
ubus call zwrt_bsp.typec set '{"PR_Swap":"source"}'
```

## 六、边界

1. **电量 / 温度保护在驱动层冻结输出**：≤10% 电量或 ≤0°C / ≥58°C 时 `policy` 直接限流为 0，UI 会提示「充电宝不可用」。
2. **反充消耗内置电池**：会将 10000mAh 电池电量供给外部设备，须预留自身运行所需电量，避免放电至关机。
3. **在设备上检查**：`online_mbb=0`、`attached=Unattached`、`power_role=sink` —— 未处于反充状态；验证须实际接入外部设备并开启。
4. 反充与 USB 口其它功能（ADB / 存储 / OTG）共享同一 USB-C 口，开启时会改变该口方向（转对外供电），与 [usb-otg-host.md](usb-otg-host.md) 的 role 切换逻辑相关。

## 七、机制摘要

U60 Pro 的充电宝为零售实装功能，链路为：触摸屏「充电宝模式」开关 → `libzte_SDKowrt.so` 的 `ZTD_SetPowerBankEnableState` → `zte_ubus_bsp_pm`（`zwrt_bsp.powerbank`）→ 内核 `powerbank_zte` 驱动（带 SoC / 温度 / 电流查表限流限压保护）→ PM7550B + SMB1394 电荷泵 → USB-C 反向输出。命令行可用 `ubus call zwrt_bsp.powerbank set '{"state":1}'` 触发；驱动层自带电量 / 温度熔断保护。

## 相关文档

- [usb-otg-host.md](usb-otg-host.md) — USB-C 角色切换
- [voip-slic.md](voip-slic.md) — 固件能力与硬件实装对照
- [ipc-protocol.md](ipc-protocol.md) — ZBUS / `zwrt_bsp.*`
- [webui-trace.md](webui-trace.md) — `zwrt_bsp.powerbank` 归属
- [README.md](../README.md) — 设备总览
