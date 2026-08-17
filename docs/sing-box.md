# sing-box 安装

> **验证说明**：本文整理仓库中的 `files/sing-box`（OpenWrt procd 启动脚本）行为。sing-box 的配置 JSON **不在本仓库**，须按所用代理与分流规则自行准备。将脚本安装到设备并 `uci set` / `start` 属写操作，执行前须备份并自行确认副作用。

## 二进制

做法与 [zerotier.md](zerotier.md) 相同：在 OpenWrt 上下载 sing-box 的 `.ipk` 并解包，将 `data.tar.gz` 解压到根目录，可执行文件放到 `/usr/bin/sing-box`（脚本中 `PROG="/usr/bin/sing-box"`）。

## procd 启动脚本（`files/sing-box`）

仓库提供 OpenWrt `procd` 风格的 `/etc/init.d/sing-box`，由 `files/sing-box` 提供，要点如下：

- `START=99`（开机阶段最后启动）。
- `USE_PROCD=1`，由 procd 托管，`procd_set_param respawn` 使进程崩溃后自动重启。
- 读取 UCI 配置包 `sing-box`（`config_load "sing-box"`）：
  - `main.enabled`（默认 `0`；**仅当为 `1` 时真正启动**）。
  - `main.conffile`（默认 `/etc/sing-box/config.json`）。
  - `main.workdir`（默认 `/usr/share/sing-box`）。
  - `main.log_stderr`（默认 `1`）。
- 启动命令：`/usr/bin/sing-box run -c <conffile> -D <workdir>`。
- **启动时将 dnsmasq 端口改为 5353**（`set_dnsmasq_port 5353`，`uci set dhcp.@dnsmasq[0].port=5353`），把 53 端口留给 sing-box 的 DNS。

## 使用（写操作）

1. 将 `files/sing-box` 复制为 `/etc/init.d/sing-box` 并 `chmod +x`。
2. 准备 `/etc/sing-box/config.json` 与 `/usr/share/sing-box` 工作目录（具体配置自备）。
3. 在 UCI 中启用并配置：

```sh
uci set sing-box.main.enabled=1
uci set sing-box.main.conffile=/etc/sing-box/config.json
uci commit sing-box
/etc/init.d/sing-box enable
/etc/init.d/sing-box start
```

4. 停止时将 dnsmasq 端口恢复为 53（`set_dnsmasq_port 53`）。

本仓库不含 sing-box 配置样本；以上仅为脚本行为说明。分流与监听规则须自行填写。
