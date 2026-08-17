# WebUI action 追踪到 ubus / daemon / 脚本

> **验证说明**：本文基于在设备（`192.168.0.1`）上的只读检查（HTTP `/ubus/` 抓包、`ubus monitor`、`strings` 归属推断）。未执行会改变设备状态的 Web action。写操作须先行备份并自行确认副作用。

U60 Pro Web 管理页为 **uhttpd + 静态前端 + HTTP-ubus JSON-RPC**，并非 LuCI 主路径（虽有 `lua_prefix` 备用）。反编译 WebUI 静态 JS 为最后手段；浏览器 Network 与设备侧 `ubus monitor` 即可得到精确的 `object` / `method` / `params`。

## 一、调用链

一次 WebUI 操作的调用链通常为：

```
浏览器 POST /ubus/?t=...
  → uhttpd (ubus_prefix=/ubus, home=/usr/zte_web/web)
  → /usr/bin/zte_web (webtoken_check / web_login)
  → /sbin/rpcd (access 权限校验)
  → 目标 ubus object.method
  → /usr/bin/zte_* daemon
  → /sbin/*.sh 或 QMI/内核/sysfs
```

## 二、Web 入口配置（UCI）

| 项 | 值 |
|---|---|
| Web 根目录 | `/usr/zte_web/web` |
| ubus HTTP 前缀 | `/ubus` |
| 完整 URL | `http://192.168.0.1/ubus/?t=<毫秒时间戳>` |
| 登录对象 | `zwrt_web`（`web_login_info` / `web_login`） |
| 会话字段 | `ubus_rpc_session`（登录后替换 32 个 `0`） |

见 [uci-config.md](uci-config.md) §九、[enable_debugging.sh](../files/enable_debugging.sh)。

### HTTP JSON-RPC 形状

```json
[
  {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "call",
    "params": [
      "<ubus_rpc_session>",
      "<object>",
      "<method>",
      { "...args..." }
    ]
  }
]
```

**只读示例**（无需登录，`web_login_info` 取 salt）：

```sh
curl -s -X POST "http://192.168.0.1/ubus/?t=$(date +%s%3N)" \
  -H "Content-Type: application/json" \
  -d '[{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","zwrt_web","web_login_info",{"":""}]}]'
```

响应含 `zte_web_sault`；完整登录与调 `zwrt_bsp.usb set` 见 [adb.md](adb.md) / [enable_debugging.sh](../files/enable_debugging.sh)。

## 三、追踪流程（5 步）

### 步骤 1：浏览器抓 HTTP payload

1. 打开 DevTools → **Network**。
2. 过滤 `ubus`。
3. 在 WebUI 执行目标 action（如刷新状态；**开关 WiFi、开调试模式属写操作**，执行前须备份并自行确认副作用）。
4. 打开 POST 请求，查看 **Request Payload**：
   - `params[1]` = **object**（如 `zwrt_wlan`）
   - `params[2]` = **method**（如 `report`、`set`）
   - `params[3]` = 参数 JSON

前端有时会将多个 JSON-RPC 对象 **batch** 于同一数组；须逐个查看 `params`。

### 步骤 2：设备侧 `ubus monitor` 确认 dispatch

SSH 到设备后（或使用仓库脚本）：

```sh
# 设备上
sh /path/to/trace_webui_ubus.sh monitor 'zwrt_wlan|webtoken|invoke'
# 或
ubus monitor | grep -E 'invoke|objpath|webtoken'
```

然后在浏览器重复同一 action。

**Web 特有中间层**（在设备上检查）：登录态请求依次经：

1. `zwrt_web.webtoken_check` — `data.z-tag` **往往等于真实 method 名**（如 `nwinfo_get_netinfo`）
2. `rpcd.access` — `{object, function}` 权限检查
3. 目标 `object.method` 的 `invoke`

`ubus monitor` 片段示例：

```
invoke: zwrt_web.webtoken_check  data.z-tag="nwinfo_get_netinfo"
invoke: rpcd.access              object="zte_nwinfo_api" function="nwinfo_get_netinfo"
invoke: zte_nwinfo_api.nwinfo_get_netinfo
```

### 步骤 3：object → daemon 映射

```sh
sh trace_webui_ubus.sh owner zwrt_bsp.usb
sh trace_webui_ubus.sh describe zwrt_bsp.usb
sh trace_webui_ubus.sh map
```

| ubus object | 实现 daemon |
|---|---|
| `zwrt_web` | `/usr/bin/zte_web` |
| `zwrt_bsp.usb` | `/usr/bin/zte_ubus_bsp_usb` |
| `zwrt_bsp.powerbank` | `/usr/bin/zte_ubus_bsp_pm` |
| `zwrt_wlan` | `/usr/bin/zte_topsw_wlan` |
| `zwrt_zte_mdm.api` | `/usr/bin/zte_topsw_mdm` |
| `zwrt_data` | `/usr/bin/zte_topsw_data` |
| `zte_nwinfo_api` | `/usr/bin/zte_topsw_nwinfo` |
| `zwrt_router.api` | `/usr/bin/zte_router` |
| `zwrt_smart_mng.api` | `/usr/bin/zte_smart_manage` |
| `zwrt_deviceui` | `/usr/bin/zte_topsw_devui`（触摸屏，非 Web） |

命名规律：`zwrt_<模块>` / `zte_<模块>_api` ↔ `/usr/bin/zte_topsw_<模块>` 或 `/usr/bin/zte_ubus_bsp_*`。

### 步骤 4：daemon → 脚本 / 底层调用

```sh
sh trace_webui_ubus.sh scripts /usr/bin/zte_ubus_bsp_usb
sh trace_webui_ubus.sh scripts /usr/bin/zte_smart_manage
```

或在 action 后再查看进程 / 日志：

```sh
ps w | grep -E 'smart_manage|usb_switch|hostapd'
logread -f    # 若 logread 可用
tail -f /tmp/usb.log
```

### 步骤 5：记录闭环

每个 action 可记一行表：

| Web action | HTTP object.method | monitor 确认 | daemon | 最终脚本/机制 |
|---|---|---|---|---|
| （示例）开 ADB | `zwrt_bsp.usb.set` | ✓ invoke list/set | `zte_ubus_bsp_usb` | `/sbin/usb/compositions/usb_switch` |
| （示例）WiFi 状态 | `zwrt_wlan.report` | ✓ + webtoken z-tag | `zte_topsw_wlan` | `toggle_wifi_onoff` / hostapd |
| （示例）网络信息 | `zte_nwinfo_api.nwinfo_get_netinfo` | ✓ z-tag 同名 | `zte_topsw_nwinfo` | modem/QMI 查询 |

## 四、已验证完整示例

### 示例 A：Web 开调试 / ADB（写操作；本文未执行，实现见文档与脚本）

| 层 | 内容 |
|---|---|
| HTTP | `params`: `[session, "zwrt_bsp.usb", "set", {"mode":"debug"}]` |
| ubus | `zwrt_bsp.usb` → `set` |
| daemon | `/usr/bin/zte_ubus_bsp_usb` |
| 脚本 | `sh /sbin/usb/compositions/usb_switch ...`（strings 实测） |

详见 [adb.md](adb.md)、[enable_debugging.sh](../files/enable_debugging.sh)。

### 示例 B：Web 仪表盘轮询「网络信息」

| 层 | 内容 |
|---|---|
| webtoken | `z-tag`: `nwinfo_get_netinfo` |
| rpcd | `access` → `zte_nwinfo_api` / `nwinfo_get_netinfo` |
| ubus | `zte_nwinfo_api.nwinfo_get_netinfo` |
| daemon | `/usr/bin/zte_topsw_nwinfo` |

### 示例 C：Web 轮询 WiFi 报告

| 层 | 内容 |
|---|---|
| webtoken | `z-tag`: `report` |
| ubus | `zwrt_wlan.report` |
| daemon | `/usr/bin/zte_topsw_wlan` |
| 机制 | 读 `wireless.zte_mbb.wifi_onoff`，控制 hostapd（见 [wifi.md](wifi.md)） |

### 示例 D：智能管理 / 家长管控

| 层 | 内容 |
|---|---|
| ubus | `zwrt_smart_mng.api.smart_mng_cutoff_set/get` 等 |
| daemon | `/usr/bin/zte_smart_manage` |
| 脚本 | `/sbin/smart_manage_deal.sh`、`/sbin/smart_set_qos_policy.sh` |
| Web 桥 | `libzte_web` 内 `access_smart_manage.c` |

详见 [smart-manage.md](smart-manage.md)。

## 五、其它手段（仍不必反编译 WebUI）

| 手段 | 用途 |
|---|---|
| **浏览器 DevTools** | 最直接：object/method/params |
| **`ubus monitor`** | 证实 dispatch；`z-tag` 暴露 Web 调用的真实方法名 |
| **`ubus -v list <obj>`** | 枚举方法签名，便于重放 `ubus call` |
| **`strings -a /usr/bin/zte_*`** | 找 `/sbin/` 脚本、UCI 键、日志 tag |
| **`/etc/init.d/zte_*`** | procd 启动哪个二进制 |
| **`libzte_SDKowrt.so` strings** | 全量 `zwrt_*` 对象名索引（见 [ipc-protocol.md](ipc-protocol.md) §三） |
| **tcpdump** | 仅当 action 使用非 `/ubus/` 的 HTTP（如智能管理云端 `ztehome.com.cn`） |
| **反编译 WebUI JS** | 前端 obfuscate 严重时的最后手段 |

### 设备限制（已知）

- `ubus listen` 可能阻塞；优先用 **`ubus monitor`**（在设备上可用）。
- 无 `strace`/`gdb` 时，用 **strings + logread + /tmp/*.log** 代替。
- 触摸屏 UI（`zte_topsw_devui`）使用 ZBUS，与 Web **并行**；见 [ipc-protocol.md](ipc-protocol.md) §一、§六。

## 六、工具脚本

仓库提供 [`files/trace_webui_ubus.sh`](../files/trace_webui_ubus.sh)：

```sh
# 复制到设备
scp files/trace_webui_ubus.sh root@192.168.0.1:/tmp/
ssh root@192.168.0.1 'sh /tmp/trace_webui_ubus.sh map'
ssh root@192.168.0.1 'sh /tmp/trace_webui_ubus.sh monitor zwrt_wlan'
```

## 七、相关文档

- [uci-config.md](uci-config.md) §九 — uhttpd / zwrt_web / rpcd
- [adb.md](adb.md) — HTTP-ubus 登录 + `zwrt_bsp.usb` 完整链
- [ipc-protocol.md](ipc-protocol.md) — ubus / ZBUS 双层模型
- [wifi.md](wifi.md) — `zwrt_wlan` / `zte_topsw_wlan`
- [smart-manage.md](smart-manage.md) — `zwrt_smart_mng.api`

> 本文为只读追踪方法；任何 `set` / `uci set` 写操作须先行备份并自行确认风险。
