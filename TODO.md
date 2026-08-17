---
domd-id: 3f7bffbe-1568-43ac-aac0-0f94f6d228f1
---

# TODO：其它隐藏接口（原 voip-slic.md 无关内容）

> 从 `docs/voip-slic.md` 拆分出来：这些内容与「VoIP / FXS 语音与音频子系统」主题无关，故单独存放，供后续按主题继续探索。
> 探索日期：2026-08-08（SSH 只读探测，一次性/非持续连接）。数据来源同 `docs/voip-slic.md`。

> ⚠️ 以下除非明确说明，均为**只读/低风险**；`set_*`/`state`/`PR_Swap` 等写接口请**先备份、确认副作用**再动。

## 云 MQTT 回连（主动外联）
- 进程 `zte_mqtt_sdk_st` 常驻（START=99），二进制内嵌阿里 `wasu/common/mqtt` 栈。
- 连接端点字符串：**`ufi.seecom.com.cn`**（ZTE 的 UFi 云）。
- 配置 `/etc/config/zwrt_mqtt`：`reportFreqInS '1000'`、`mqttOnreportEnable '0'`。
- 日志：`/tmp/mqttlog.txt`。**注意**：这是设备向中兴云上报的通道，如做魔改可关注 `mqttOnreportEnable` 开关/下行控制话题。

## 移动电源（powerbank）模式
- `zwrt_bsp.powerbank`（`ubus -v list`）：`set{"state"}`, `get{"property"}`。
- 实测 `get`（不带参数）超时；`set state` 未执行（写接口）。
- 配合 `zwrt_bsp.charger.list` 的 `otg_powerbank_state` 与 **`zwrt_bsp.typec`**：这是 **充电宝反向输出**（OTG/PD 反向）的入口 —— 把 10000mAh 电池当充电宝给手机反向充电。

## Type-C 角色交换（PD）
- `zwrt_bsp.typec.set`：`{"PR_Swap"}`, `{"DR_Swap"}` —— 主动发起 Power Role / Data Role 交换（PD 协议握手）。
- 实测当前：`power_role=sink`、`data_role=host`、`cc_attch_state=1`（连接中）。

## PM / 开机原因 / RTC 闹钟
- `zwrt_bsp.pm`：`power_on_reason`（实测 `1`）、`set_device{label,action}`（疑似控制 GPIO 类外设）。
- `zwrt_bsp.rtc`：`ui_set_alarm`（UI 闹钟，写接口）。RTC 为 `/dev/rtc0`，无电池时时间停在 1970-01-01。

## 存储 / 媒体共享
- `zwrt_samba`：`get_settings`/`get_usb_info`/`set_settings{switch}`/`dlna_settings{enabled,friendly_name,port}` —— 即插 U 盘共享 + DLNA。与 README 的 usb 口 host 复用相关。
- `zwrt_bsp.led.list`：当前 LED 名 `led:lcd`、`mmc0::`。

## 温度 / 断网保护 / 隧道
- `zwrt_bsp.thermal`：`get_cpu_temp`（实测 `cpuss_temp: 40`）、`get_policy`/`set_policy`。
- `zwrt_cutoff_protect.api`：断网保护（`check_url_valid`、`data_abnormal{policy,cid,datastall_status}`）。
- 隧道栈非常全：`zwrt_tunnel.{gre,ipsec,l2tp,pptp,vxlan}` + `zwrt_tunnel.config`（up/down/cb 拨号回调）—— 比 README 里提到的更完整。

## 后续可玩方向
1. **手机→U60Pro 反向充电**（powerbank + typec）：**物理上最实用的隐藏玩法** —— 通过 `zwrt_bsp.typec.set PR_Swap/DR_Swap` 或 `zwrt_bsp.powerbank set state` 让 10000mAh 电池给手机充电。需先手测 USB 口当前 role（`data_role` 已是 host）。
2. **抓云上报**：`zte_mqtt_sdk_st` 连 `ufi.seecom.com.cn`，可抓包看它上报什么、能否被下发控制（关注 `mqttOnreportEnable`）。
3. **Samba + USB 存储**：接 U 盘开共享，或测 DLNA。
