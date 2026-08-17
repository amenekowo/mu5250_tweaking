# WLAN（WiFi）开关机制

> **验证说明**：本文基于在设备上的只读检查（SSH `root@192.168.0.1`：UCI 读取、dmesg、uname、运行态接口查询），以及对 `/usr/bin/zte_topsw_wlan` 的 strings 分析。`uci set wireless.zte_mbb.wifi_onoff` **尚未在设备上执行**。`files/guest_bssid.sh` **已在设备上执行**：自定义 MAC 写入 UCI / `wlan_mac.bin` 后，经 guest VAP 重建可在空口生效；测毕已 `reset` 恢复出厂 guest MAC。写操作须先行备份并自行确认副作用。

WLAN 总开关为 `wireless.zte_mbb.wifi_onoff`，由 `zte_topsw_wlan` 执行射频启停。即便 iface 层 `disabled=0`，只要总开关为 0，接口也不会被启用。

## 一、总开关：`wireless.zte_mbb.wifi_onoff`

WLAN 的**总开关**不在 `/etc/config/wireless` 的某个 wifi-device / iface 里，而在 `feature 'zte_mbb'` 段：

```sh
uci show wireless.zte_mbb
# wireless.zte_mbb=feature
# wireless.zte_mbb.lbd='1'                 # LBD（负载均衡/换频）使能
# wireless.zte_mbb.wifi_onoff='0'          # WLAN 总开关（1=开，0=关）
# wireless.zte_mbb.wifi_onoff_by_user='0'  # 是否为用户手动操作（区别于后台/回程自动）
# wireless.zte_mbb.mesh_onoff='0'
# wireless.zte_mbb.mesh_role='router'
# wireless.zte_mbb.mlo='0'
# wireless.zte_mbb.mlo_num='2'
# wireless.zte_mbb.wifi6_switch='1'
```

- `wifi_onoff`：整个无线功能的开关。
- `wifi_onoff_by_user`：标志此次开关由**用户手动**触发，或由系统（如回程检测、mesh、过热保护）自动触发。

## 二、物理 / 逻辑接口配置

`/etc/config/wireless` 中还有 wifi-device（射频）与 wifi-iface（BSS）两级：

- **wifi-device**（`wifi0`=2.4G、`wifi1`=5G，`type 'qcacld32'`，芯片 `wcn7851`）：
  - `disabled '1'` 表示**该射频被禁用**。在设备上检查时，2G / 5G 两个 radio 均为 `disabled=1`。
  - main 与 guest iface 中 `disabled '0'` 的是 BSS 层开关。
- **wifi-iface**：`main_2g`（wlan0）、`main_5g`（wlan2）、`guest_2g`（wlan1）、`guest_5g`（wlan3），以及 `backhaul_2g/5g`（隐藏回程 SSID，以设备 MAC 命名，已隐去）。
  - 主 SSID：`<SSID>`（`sae-mixed` / WPA3 兼容），密码 `<WiFi密码>`（key 字段）。
  - Guest SSID：`<Guest SSID>`（以设备 MAC 生成，已隐去）。
  - 即便 iface 层 `disabled=0`，只要总开关 `wifi_onoff=0`，接口也不会被启用。

相关 UCI 段落：`qcmap_wlan` / `qcmap_wlan_current`（apifname / staifname、guest 计数等）、`hostapd`（hostapd 实际配置）。

## 三、开关处理 daemon：`zte_topsw_wlan`

WLAN 开关的实际执行者是 `/usr/bin/zte_topsw_wlan`（procd 服务，`/etc/init.d/zte_topsw_wlan`，START=49）：

```sh
# /etc/init.d/zte_topsw_wlan
procd_set_param command /usr/bin/zte_topsw_wlan
procd_add_reload_trigger wireless   # 监听 wireless 配置变化自动 reload
```

其二进制内与开关相关的符号 / 字符串（strings）：

```
wifi_onoff
wifi_onoff_by_user
toggle_wifi_onoff
handle_feature_wifi_onoff_change
check_feature_wifi_onoff_when_startup
lbd[%s], eacs[%s], beamforming[%s], wifi_onoff[%s]
[%s] wifi_onoff = %s
mesh_onoff / sta_onoff / wifi_thermal_onoff / wps_onoff   # 其它并行的 onoff 开关
```

即：`zte_topsw_wlan` 读取 `wireless.zte_mbb.wifi_onoff`，通过 `toggle_wifi_onoff` / `handle_feature_wifi_onoff_change` 完成射频的启停，并一并维护 `wifi_onoff_by_user`。`ubus call zwrt_wlan reload` 可触发重载。

## 四、关闭 WLAN 后的运行态

触发关闭（`handle_wifi_onoff 0`）后的 dmesg 与服务日志：

```
zteqcawifi 1037: handle_wifi_onoff 0                    # 切总开关=0
hostapd-daemon: stop wlan0 ifdown
hostapd-daemon: stop wlan2 ifdown
```

关闭后运行态：

- `br-lan/brif` 中只剩 `eth0`，`wlan0/1/2/3` 网络接口**全部消失**（`/sys/class/net/wlan*` 不存在）。
- 2.4G / 5G 的 hostapd 实例被停，BSS 不再广播。

## 五、恢复 WLAN（写操作，尚未执行）

```sh
uci set wireless.zte_mbb.wifi_onoff=1
uci commit wireless
# 触发生效（二选一）：
ubus call zwrt_wlan reload
# 或重启无线相关服务
/etc/init.d/zte_topsw_wlan restart
```

> ⚠️ **写操作提醒**：以上命令会改动设备配置并启动射频，属写操作，本文验证过程中**未执行**。执行前须备份并自行确认副作用。

## 六、与 NFC 的关系小结

详见 [nfc.md](nfc.md)「与 WLAN 开关的关系」。

- `zte_topsw_nfc` 会监听 `wifi_onoff` / guest 变化，以联动刷新碰一碰所写入的 WiFi 索引（SSID / 密码同步），但**不会因此禁用 NFC 芯片**。
- 关闭 WLAN 后：NFC 守护进程、芯片 0x57、GPIO 均正常；只是手机没有可关联的 SSID 广播，故碰一碰无法连上 WiFi。
- 恢复碰一碰的前提是先恢复 `wifi_onoff=1`。

## 七、其它无线相关服务 / 脚本

- `/etc/init.d/hostapd-daemon.init` / `hostapd-global.init`：hostapd 实例管理。
- `/etc/init.d/repacd`：RePEater AP Coordination（回程 / 换频）守护。
- `/etc/init.d/ezmesh`：ZTE mesh 组网。
- `/etc/init.d/wlan` / `wlan-cnss.init`：wlan 驱动 / CNSS 初始化。
- `/usr/sbin/zte_wlan_func.sh`、`zte_start_wlan_at_boot.sh`、`zte_post_wifi.sh` 等：WLAN 初始化 / 后处理脚本。
- `/usr/sbin/hostapd`、`hostapd_cli`、`wifi` 等：hostapd 与底层 wifi 工具。

设备无线芯片：Qualcomm **wcn7851**（`wireless.readonly.chipset='wcn7851'`，phy_map `radio2-wifi0` / `radio5-wifi1`）。

## 八、Guest BSSID（MAC）机制与修改

Guest 与 Main **前缀相同、仅差 1 个字节**，手机扫描时二者看起来接近，属出厂设计；同频段上 Guest 与 Main **不应**逐字节完全相同。

### 接口与出厂 MAC 关系

| wifi-iface | 接口 | 角色 | 出厂 MAC 规律（相对 main 同频段 +1） |
| ---------- | ---- | ---- | ------------------------------------ |
| `main_2g` | wlan0 | 主 2.4G | 基址，如 `…f8:fd:5f` |
| `guest_2g` | wlan1 | Guest 2.4G | 第 4 字节 +1，如 `…f9:fd:5f` |
| `main_5g` | wlan2 | 主 5G | 如 `…00:fd:5f` |
| `guest_5g` | wlan3 | Guest 5G | 第 4 字节 +1，如 `…01:fd:5f` |

`br-lan` 的 MAC 通常等于 `main_2g`（wlan0）。关联 Guest 时看到的网关 MAC 也会与主 WiFi 相同，勿与 Guest BSSID 混淆。

### MAC 来源

1. **UCI**：`wireless.guest_2g.macaddr` / `wireless.guest_5g.macaddr`
2. **驱动 MAC 表**：`/etc/misc/wifi/wlan_mac.bin` 的 `Intf{0..3}MacAddress`
3. **建 VAP 时**由 qcacld32 读 `wlan_mac.bin` 赋给 `wlan1` / `wlan3`；BSSID = 接口自身 MAC

四 VAP 全开时的槽位（与 `/lib/wifi/qcacld32.sh` 的 `update_macaddress` 一致）：

```
Intf0 = main_2g   → wlan0
Intf1 = guest_2g  → wlan1
Intf2 = main_5g   → wlan2
Intf3 = guest_5g  → wlan3
```

### 仅改 UCI 或重启 guest 不更新已运行接口的 MAC

已运行的 `wlan1` / `wlan3` **不会**因改 UCI、`/etc/init.d/zte_topsw_wlan restart` 或 Web 端开关 guest 而更新 MAC。须**销毁并重建 guest VAP**：

1. `ubus call zwrt_wlan set` 关闭 guest（`disabled=1`），等 `wlan1` / `wlan3` 消失
2. 写 UCI，并同步完整四个接口槽位到 `wlan_mac.bin`
3. 再 `ubus call zwrt_wlan set` 开启 guest（`disabled=0`）

Web 端开启 guest 等价于 `zwrt_wlan set` 里对 `guest_2g` / `guest_5g` 设置 `disabled` 与 `guest_active_time`（见 `wifi_guest.js` → `setWifiMeshGuestSwitch`）。

### 使用 `files/guest_bssid.sh`

部署到 `/data/guest_bssid.sh` 后：

```sh
# 查看 runtime / UCI / wlan_mac.bin
/data/guest_bssid.sh status

# 2.4G 与 5G 设不同 MAC（推荐）
/data/guest_bssid.sh aa:bb:cc:dd:ee:01 aa:bb:cc:dd:ee:02

# 恢复出厂（需先写入 /data/guest_bssid.factory，含 RESET_2G / RESET_5G）
/data/guest_bssid.sh reset
```

首次使用前在设备上保存出厂值（每台不同，勿硬编码进仓库）：

```sh
printf 'RESET_2G=%s\nRESET_5G=%s\n' \
  "$(uci get wireless.guest_2g.macaddr)" \
  "$(uci get wireless.guest_5g.macaddr)" \
  > /data/guest_bssid.factory
```

验证：扫描 Guest SSID（`ZTE_…_Guest`），BSSID 应与 `status` 中 `runtime=` 一致；或 `cat /sys/class/net/wlan1/address`。

> ⚠️ **写操作**：会改 `wireless` 与 `wlan_mac.bin`，并短暂断连 guest。脚本自动备份 `/etc/config/wireless` 到 `/data/wireless.bak.*`。该脚本已在设备上执行，测毕已 `reset` 恢复出厂 guest MAC。

## 相关文档

- [nfc.md](nfc.md) — `zte_topsw_nfc` 与 `wifi_onoff` 联动
- [uci-config.md](uci-config.md) — `wireless` / `zte_mbb` 等 UCI 配置
- [ipc-protocol.md](ipc-protocol.md) — `zte_topsw_*` 进程间通信
- [hardware.md](hardware.md) — 无线芯片 wcn7851
- [README.md](../README.md) — 设备总览
