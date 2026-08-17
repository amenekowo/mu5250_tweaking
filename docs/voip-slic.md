# 语音 / FXS（VoIP）与音频子系统

> **验证说明**：本文基于对固件二进制与 UCI 配置的静态分析，以及在设备上的只读检查（`ubus` / `ps` / `strings` / `dmesg` / sysfs / config，以及 `qcrilnr-console-app` 连接运行中 `qcrilNrd` 的 RIL/QMI 查询）。`zwrt_bsp.audio` 的 `set_*` 方法、状态切换与呼叫**均未在设备上执行**；写操作须先行备份并自行确认副作用。

U60 Pro 固件内含一套完整但原厂未公开的 VoIP / FXS 程控电话子系统（SIP、SLIC 电话线、DTMF、振铃、回声消除等）。上述能力属于 **X75 / SDX75 modem 平台底座**；零售 U60 Pro 上 **SLIC 驱动未加载**（见「休眠状态」），整套电话栈处于 **休眠**。零售硬件大概率**没有**物理电话插口，但固件仍包含驱动、应用、SIP 栈与两路 FXS。

SLIC/FXS 为接模拟座机的模拟电话线接口，与「SIM 卡打移动电话」是两套独立路线：缺 SLIC **不影响** SIM 语音的判断（见「四、SIM 语音 / IMS」）。固件 RIL 客户端栈完整，SIM 能注册到 LTE（VoLTE 基础），但设备以 PS-Only（纯数据域）运行、IMS 语音未拨通，且当前卡为物联网/数据卡（不开语音资费）。零售机无法作为手机拨号，根因**不在 SLIC、也不在固件未实现 IMS**，而在数据卡与 PS-Only 的定位。

## 一、双路 FXS 电话线接口（SLIC）

`zwrt_bsp.audio`（`/usr/bin/zte_ubus_bsp_audio`，procd 管理）内部为 ZTE 语音子系统。由二进制字符串还原的能力：

| 能力 | 证据（二进制字符串） |
|---|---|
| **两条 FXS 通道** | 同时打开 `/dev/slic-0` 和 `/dev/slic-1` |
| **芯片就绪轮询** | `slic_get_chip_ready_msg`，读 `/sys/class/slic/slic-0/chip_ready` |
| **摘机/挂机 hook** | `on-hook` / `off-hook`；`BSP_SLIC_HOOK_EVENT`（ubus send） |
| **DTMF 检测** | `BSP_SLIC_DTMF_EVENT` / `slic will generate dtmf tone` |
| **振铃 ring** | `set_ring`；`zte_voip_agmplay` 打开 `/dev/slic-0` 播放 |
| **提示音 tone** | `set_tone`：`TONE_SET_DIRECTION/TYPE/ACTION` |
| **音量** | `zte_voice_set_audio_volume` / `rx_volume_adjust.sh` / `/tmp/zte_volume` |
| **播放文件** | `set_play_file` / `zte_voip_agmplay file.wav [-D card]` |
| **静音控制** | `SLIC_SET_TX_MUTE ON/OFF` |
| **工作模式** | `zwrt_voice.config.voice_work_type` → `VOIP` / `VOICE_ODU` / `VOICE` |
| **SLIC 上电** | `sh /sbin/zte_audio/slic_up.sh` |

`zwrt_bsp.audio` 的 ubus 方法（在设备上执行 `ubus -v list zwrt_bsp.audio`）：

```
get_hook_state, BSP_SLIC_CHIP_READY, BSP_SLIC_HOOK_EVENT,
BSP_SLIC_DTMF_EVENT, set_path, set_tone, set_ring, set_volume,
set_play_file, set_call_type
```

在设备上只读调用：`get_hook_state` → `{"hook_state": "on-hook"}`，`BSP_SLIC_CHIP_READY` → `{"chip_ready": 1}`（此值为 daemon 上报，不依赖实际 SLIC 硬件）。

### SLIC 芯片支持（来自 voice_call.sh / slic_up.sh）

```sh
slic_chipmodel=$(cat /sys/class/slic/slic-0/chip_model)
case in
  SI32176 ) # Silicon Labs 单路 SLIC
  LE9643  ) # Lantiq/Intel SLIC
  AS1630B ) # leadram 滑动
esac
```

- `restart_func`（SDXPINN 分支）：`killall -10 hal_voice_test; hal_voice_test -i 11C05000 -d 2,2147483664 &`
- DTMF 采集：`zte_dtmf_agmcap /tmp/dummy.wav -D 100 -d 101 -c 1 -n 1 -p 160 -r 8000 -b 16 -i AUXPCM-LPAIF-TX-PRIMARY`
- PCI/MMIO 基址 `11C05000`（SDX 平台语音控制器）

### 休眠状态

- `/sys/class/slic`（`/sys/class/slic/slic-0/chip_model`、`chip_ready`）**不存在** —— SLIC 内核驱动未在零售机加载。
- `/dev/slic-0`、`/dev/mbb_voip`、`/dev/slic-1` **不存在**。
- 声卡只有 0 张：`0: sdx-tavil-i2s-snd-card`（`/proc/asound/cards`）。dmesg 里 `i2c@988000/tavil_codec` 的 I2C 创建 **失败/被跳过**（`modalias failure`），即 codec/Tavil 与 SLIC 相关的 I2C 外设未枚举。—— 与「零售机不带语音」一致。

## 二、VoIP（SIP）栈

`/etc/config/zwrt_voip`（约 100 项）给出完整 SIP 注册客户端配置。节选：

```text
voip_vpline_enable 1         voip_vpline_status Up
voip_register_status registered
voip_registration_server ''  voip_registration_server_port 5060
voip_proxy_server ''         voip_proxy_server_port 5060
voip_user_agent_transport UDP   voip_user_agent_port 5060
voip_vp_line_codec_list_priority1 2   (G.711A? 码字表)
voip_rtp_local_port_min 4000  max 4010   rtp_telephone_event_pt 97
voip_dtmf_method RFC2833
voip_echo_cancellation_enable 1   voip_transmit_gain 140  voip_receive_gain 140
voip_call_hold_enable 1  voip_call_transfer_enable 1  voip_three_way_talking_enable 1
voip_call_waiting_enable 1  voip_call_fwd_*_enable 0
voip_zte_mbb_flag TRUE     voip_sip_timer_t1/t2/t4 ...
```

统计字段表明这版固件**曾经跑过话务**：

```text
voip_stats_outgoing_call_attempted '11'
voip_stats_outgoing_call_connected '9'
voip_stats_outgoing_call_answered   '9'
voip_stats_incoming_call_received   '1'
voip_stats_incoming_call_answered   '0'
voip_stats_calls_dropped            '0'
voip_stats_outgoing_call_failed     '2'
voip_hook_status                    'PhoneOnhook'
```

> 这些是工厂/ODM 打样或某运营商定制版遗留的统计，不一定是这台零售机真实使用。当前**没有** SIP/VoIP 守护进程在运行（`ps` 无 voip/voipa 进程）。

### 相关二进制（全在，但当前未常驻）

| 文件 | 说明 |
|---|---|
| `/usr/bin/zte_voip_agmplay` | 往 `/dev/slic-0` 播 WAV；`Usage: file.wav [-D card] [-d device] [-i device_id]` |
| `/usr/bin/hal_voice_test` | 语音呼叫测试：`hal_voice_test -i 11C05000 -d 2,2147483664` |
| `/usr/bin/zte_route_hal_voice_test` | ODU 语音呼叫，含 `-l length -m multi_call -r record -p/-e incall_playback -n stream_type` |
| `/usr/bin/agmvoiceui` | Voice UI 事件测试（AUXPCM-LPAIF 主路径） |
| `/usr/bin/mm_audio_ftm` | 音频 FTM（PCM_LL_PLAYBACK / VOIP_MBDRC / VOIP_FLUENCE_PRO 等） |
| `/usr/bin/agmplay`,`qtitinyplay`,`hal_play_test` | 播放工具 |

## 三、音频子系统（ALSA/AGM + Tavil codec + 语音）

- 声卡：`sdxtavili2ssndc`（`sdx-tavil-i2s-snd-card`），PCM 设备 `pcmC0D0p`（playback）、`pcmC0D1c`（capture）、`controlC0`、`timer`。
- dmesg：`audio_heap_region`、`qcom,audio` DMA-BUF heap、`agm_server`（ujail 权限进程 `-U media -G media`）—— 高通 AudioReach 架构。
- I2C：`i2c-0..4`；`i2c@988000` 上的 `tavil_codec` 在零售机未成功枚举。
- 脚本：`/sbin/zte_audio/{slic_up.sh,voice_call.sh,start_audio_zte.sh,odu_policy.sh,rx_volume_adjust.sh,loopback_pcm.sh}`、`/etc/init.d/init_audio.init`（`start_audio_le start`）、`/etc/init.d/zte_ubus_bsp_audio.init`。

### ubus 音频控制（只读可查）

```sh
ubus call zwrt_bsp.audio get_hook_state
ubus call zwrt_bsp.audio BSP_SLIC_CHIP_READY
# set_path / set_tone / set_ring / set_volume / set_play_file / set_call_type 为写接口（本文未在设备上执行）
```

## 四、SIM 语音 / IMS（VoLTE）

> 本节为在设备上用**高通 RIL/QMI 只读查询**做的交叉验证，用以判断在无 SLIC、或接入 USB 声卡的情况下，能否用 SIM 卡打电话。**全部为只读查询，未发起任何呼叫或写操作。**

### 4.1 RIL / IMS 客户端栈

- RIL daemon **`qcrilNrd` 正在运行**（`ujail -n qcril`，`/usr/bin/qcrilNrd`），SELinux domain `qcril.subj`，socket `/dev/socket/qcrild`。
- 配套**交互控制台 `/usr/bin/qcrilnr-console-app`**（QCRIL Client Lib v1.0），可直连运行中的 RIL 做查询，菜单含 `Phone_Menu / Dialer / RILCall / IMS_Menu / Data / Simcard / SMS / OEMHOOK`。
- RIL 客户端栈**具备完整 IMS 能力接口**（二进制符号可见）：`imsDial`、`getCurrentCalls`、`getImsRegState`、`Registration_Status`、`QueryServiceStatus`、`imsSendRttMessage`、`imsDtmf*`、呼叫前转/保持/多方/DEFECT 等。
- modem 侧控制通道：`QCMAP_ConnectionManager`（纯数据管理，`voice/ims/volte` 字符串计数 = 0）、`diag-router`、`ipacmdiag`、SMD 通道 `smd7/11/21/22`、`glink_modem`。

### 4.2 RIL 只读查询结果（qcrilnr-console-app）

| 查询项 | 结果 | 含义 |
|---|---|---|
| **IMS Registration_Status** | `RIL_IMS_REG_STATE_NOT_REGISTERED` | IMS 全局**未注册** |
| **IMS QueryServiceStatus** | SMS 服务 = `DISABLED`；UT（补充业务） = `ENABLED` 且 `RIL_IMS_REG_STATE_REGISTERED` / `RADIO_TECH_LTE` | IMS 仅**部分服务**注册（UT 已注册，SMS/语音未通） |
| **Voice Registration** | `RIL_REG_ROAMING`，`RADIO_TECH_LTE`，运营商 **CMCC / China Mobile**，LTE 小区（MCC 460 / MNC 00 / LAC 20000） | SIM 在网、LTE 级语音注册基础在 |
| **Voice Radio Tech** | `16` = RADIO_TECH_LTE | VoLTE 基础（LTE 承载）就绪 |

> 解读：`VOICE REGISTRATION: LTE / REG_ROAMING` 属 **NAS/网络层的「语音承载上报」**，不等于这张卡开通了可用语音业务。真正可用的判定要看系统侧 `domain_stat` 与 IMS 语音服务状态（见下）。

### 4.3 系统侧：PS-Only（纯数据域），语音业务未启用

- `zte_nwinfo`：**`domain_stat = 'PS_ONLY'`**、`network_type = 'SA'`、`net_select = 'WL_AND_5G'`。
  → 只注册了**分组交换域（PS = 数据）**，**没有电路交换域或 IMS 语音承载**。这是 5G 数据 modem 的典型配置。
- `zwrt_wms`：`wms_ims_flag = '0'`、`zte_wms_ims_switch = '1'`（IMS 开关默认关）。

### 4.4 当前 SIM 卡判定：物联网/数据卡，不带语音资费

设备为**双卡**，`sim_card_*` 字段记录了两张卡（具体 IMSI / ICCID / 号码为个人标识，不予记录）。仅保留与本分析相关的判定信息：

| 方面 | 判定 |
|---|---|
| 卡的 MCC/MNC（IMSI 前段） | 在用卡 IMSI 前缀 **MCC 454 / MNC 12** —— **物联网/虚拟运营商/M2M 专用段**（非大陆普通手机卡号段） |
| 卡发行商（ICCID 前缀段） | 89852 段 = 中国联通系（CMLink 类）；SPN 品牌为 **CMLink**（联通境外虚拟运营商，号码为 `852...` 中国香港区号） |
| 另一张卡（SIM2） | 中国大陆移动段（46008） |
| 实际注册 PLMN | `zte_nwinfo.plmn_info.rplmn_mccmnc 46000`（CMCC / China Mobile），`name='CMLink'` —— 当前附着/漫游到中国移动 CMCC（460）的 5G SA 网络 |

> ⚠️ ZTE 内部 `sim_*` / `sim2_*` 字段与物理槽位的对应关系并不总是直白——这里只保留**判定所需的段信息，不呈现具体号码**。无论哪张卡，本结论均成立：**当前网络注册为 `domain_stat=PS_ONLY`（纯数据域），且 IMS 语音服务 DISABLED**，表明语音业务未开通。

→ **当前 SIM 组合为物联网/数据卡**（IMSI 454 段 / 联通系段 ICCID / CMLink 品牌），只开通了数据业务。语音资费/IMS 语音并未开通。

### 4.5 结论

- **SLIC 与 SIM 语音无关**。固件实现了完整 RIL/IMS 客户端栈，且 SIM 已注册 LTE。
- **瓶颈**（分层）：
  1. **卡侧**：这张物联网/数据卡本身**不开语音资费**（IMS 语音服务 DISABLED）。
  2. **设备侧**：`domain_stat = PS_ONLY`，只做数据域，IMS 语音未拨通。
  3. **音频侧**：即便打通，语音音频仍绑定内部 LPAIF/Tavil codec 路径，USB 声卡/普通 ALSA 接不进 modem 语音通路（见三、音频子系统）：内核 `# CONFIG_SND_USB_AUDIO is not set`，`modprobe snd-usb-audio` 失败。
- **根因**：这台 5G 随身 WiFi 无法作为手机拨号，是因为 **数据卡 + PS-Only + 语音资费未开**，而不是 SLIC / 固件能力缺失。

## 五、写操作与未执行接口

> 以下除非明确说明，均为**只读/低风险**；`set_*` 等写接口**均未在设备上执行**，执行前须备份并确认副作用。

SLIC 驱动未加载，需硬件上确有 SLIC 芯片与物理电话端子才有意义；可先 `cat /sys/class/slic/*` 确认驱动节点是否存在。`zwrt_bsp.audio` 的 `set_tone` / `set_play_file` 可向 ALSA 放音（`agmplay` / `qtitinyplay`）；无 SLIC 硬件时 `set_ring` / `set_tone` 可能无输出，属预期。

## 六、结论

- 固件含完整的双路 FXS 电话/VoIP/SIP 栈（SLIC + Tavil codec + 呼叫统计），零售硬件未带语音，SLIC 驱动未加载，栈处于休眠。
- 用 SIM 打电话：固件 RIL/IMS 客户端栈完整、SIM 能注册 LTE（VoLTE 基础），但设备 `domain_stat=PS_ONLY`（纯数据域）+ 当前卡为物联网/数据卡（454 IMSI、不开语音资费）+ IMS 语音 DISABLED —— **无法作为手机拨号，根因是数据卡与 PS-Only，而非 SLIC 或固件能力缺失**。

## 相关文档

- [uci-config.md](uci-config.md) — `zwrt_voice` / `zwrt_wms`（§十一）
- [ipc-protocol.md](ipc-protocol.md) — `zte_topsw_mdm` / ubus
- [README.md](../README.md) — 设备总览
