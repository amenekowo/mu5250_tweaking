#!/bin/sh
# trace_webui_ubus.sh — 把 WebUI action 追到 ubus object/method 及实现 daemon/脚本
#
# 用法（在设备上运行，或 ssh root@192.168.0.1 'sh -s' < files/trace_webui_ubus.sh）：
#   sh trace_webui_ubus.sh monitor [filter]     # 实时 ubus monitor，可选 grep 过滤
#   sh trace_webui_ubus.sh list-objects         # 列出 zwrt_/zte_ ubus 对象
#   sh trace_webui_ubus.sh describe <object>    # 显示 object 的方法签名
#   sh trace_webui_ubus.sh owner <object>       # 猜测 object 归属 daemon（strings 匹配）
#   sh trace_webui_ubus.sh scripts <binary>     # 从 daemon 二进制提取 /sbin/ /usr/bin/ 路径
#   sh trace_webui_ubus.sh map                  # 常用 object -> daemon 对照表
#
# 浏览器侧：DevTools -> Network -> 过滤 "ubus" -> 查看 POST body 中
#   params: [session, "object_name", "method_name", { ...args }]
#
# 参见 docs/webui-trace.md

set -e

FILTER="${2:-}"

cmd_monitor() {
	echo "=== ubus monitor (Ctrl+C 停止) ==="
	echo "请在浏览器/WebUI 执行目标 action；输出中关注 invoke + objpath/method"
	echo "Web 请求通常先出现 zwrt_web.webtoken_check (z-tag=方法名)，再 rpcd access，再 invoke"
	echo ""
	if [ -n "$FILTER" ]; then
		ubus monitor 2>/dev/null | grep --line-buffered -E "$FILTER|objpath|invoke|lookup"
	else
		ubus monitor 2>/dev/null
	fi
}

cmd_list_objects() {
	echo "=== zwrt_/zte_ ubus objects ==="
	ubus list 2>/dev/null | grep -E '^zwrt_|^zte_' | sort
}

cmd_describe() {
	obj="$1"
	if [ -z "$obj" ]; then
		echo "usage: $0 describe <object>" >&2
		exit 1
	fi
	ubus -v list "$obj" 2>/dev/null || echo "object not found: $obj" >&2
}

# 在常见 daemon 二进制里搜索 object 名字符串，推断归属
cmd_owner() {
	obj="$1"
	if [ -z "$obj" ]; then
		echo "usage: $0 owner <object>" >&2
		exit 1
	fi
	echo "=== searching binaries for object: $obj ==="
	for bin in /usr/bin/zte_* /usr/bin/zte_web /sbin/rpcd; do
		[ -f "$bin" ] || continue
		if strings -a "$bin" 2>/dev/null | grep -qF "$obj"; then
			echo "MATCH: $bin"
		fi
	done
	echo ""
	echo "=== running processes (zte_*) ==="
	ps w 2>/dev/null | grep -E '[/]usr/bin/zte_|[/]sbin/rpcd' | grep -v grep || true
}

cmd_scripts() {
	bin="$1"
	if [ -z "$bin" ] || [ ! -f "$bin" ]; then
		echo "usage: $0 scripts </usr/bin/daemon>" >&2
		exit 1
	fi
	echo "=== shell/binary paths referenced by $bin ==="
	strings -a "$bin" 2>/dev/null | grep -E '^/(sbin|usr/bin|etc/init\.d)/' | sort -u | head -80
}

cmd_map() {
	cat <<'EOF'
常用 ubus object -> 实现 daemon -> 典型下游脚本/动作

object                  daemon                      下游（strings/文档）
----------------------  --------------------------  ------------------------------------------
zwrt_web                /usr/bin/zte_web            web_login, webtoken_check, rpcd access 网关
(rpcd)                  /sbin/rpcd                  access {object,function} 权限校验
zwrt_bsp.usb            /usr/bin/zte_ubus_bsp_usb     sh /sbin/usb/compositions/usb_switch
zwrt_bsp.powerbank      /usr/bin/zte_ubus_bsp_pm      sysfs powerbank_zte
zwrt_wlan               /usr/bin/zte_topsw_wlan       toggle_wifi_onoff, hostapd
zwrt_zte_mdm.api        /usr/bin/zte_topsw_mdm        SIM/QMI, ubus notify
zwrt_data               /usr/bin/zte_topsw_data       dsi_start_data_call (QMI)
zte_nwinfo_api          /usr/bin/zte_topsw_nwinfo     注册/频段
zwrt_router.api         /usr/bin/zte_router           DHCP/NAT
zwrt_smart_mng.api      /usr/bin/zte_smart_manage     /sbin/smart_manage_deal.sh
zwrt_qcmap_cli          QCMAP_CLI                   QCMAP 数据面
zwrt_deviceui           /usr/bin/zte_topsw_devui      LVGL 屏 UI（非 Web）

WebUI HTTP 路径：POST http://<gw>/ubus/?t=<ms>
JSON-RPC: {"method":"call","params":[session, object, method, args]}
未登录 session 为 32 个 0；登录见 files/enable_debugging.sh
EOF
}

usage() {
	echo "usage: $0 {monitor|list-objects|describe|owner|scripts|map} [arg]" >&2
	exit 1
}

case "${1:-}" in
monitor)       cmd_monitor ;;
list-objects)  cmd_list_objects ;;
describe)      cmd_describe "$2" ;;
owner)         cmd_owner "$2" ;;
scripts)       cmd_scripts "$2" ;;
map)           cmd_map ;;
*)             usage ;;
esac
