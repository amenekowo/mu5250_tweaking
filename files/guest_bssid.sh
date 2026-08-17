#!/bin/sh
# 修改 guest WLAN 的 BSSID(MAC 地址)
#
# 原理: BSSID 即接口 MAC，由 wireless.guest_* .macaddr 与 /etc/misc/wifi/wlan_mac.bin
#   的 Intf 槽位决定。MAC 在 guest VAP *创建* 时写入，已运行的 wlan1/wlan3 不会因
#   改 UCI 或 restart zte_topsw_wlan 而变更；须先关 guest、同步配置、再开 guest 重建 VAP。
#   四 VAP 全开时槽位: Intf0=main_2g, Intf1=guest_2g, Intf2=main_5g, Intf3=guest_5g。
#
# 用法:
#   guest_bssid.sh <mac>                  # 2.4G 与 5G guest 设同一 MAC
#   guest_bssid.sh <mac2g> <mac5g>        # 2.4G/5G 分别设(推荐)
#   guest_bssid.sh reset                  # 恢复出厂(见 /data/guest_bssid.factory)
#   guest_bssid.sh status                 # 查看当前 BSSID，不改动
#
# 注意:
#   - BSSID 须为单播地址; 2.4G/5G guest 同 MAC 且都桥接 br-lan 可能冲突
#   - guest 关闭时仅写 UCI/wlan_mac.bin，启用 guest 后才会在空口生效
#   - 写操作前自动备份 /etc/config/wireless 到 /data

set -u

UCI_2G=wireless.guest_2g
UCI_5G=wireless.guest_5g
IF_2G=wlan1
IF_5G=wlan3
FACTORY_FILE=/data/guest_bssid.factory
GUEST_ACTIVE_TIME="${GUEST_ACTIVE_TIME:-$(uci get wireless.guest_2g.guest_active_time 2>/dev/null || echo 240)}"

norm_mac() {
	[ $# -eq 1 ] || return 1
	case "$1" in
		[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]) ;;
		*) return 1 ;;
	esac
	echo "$1" | tr 'A-F' 'a-f'
}

check_unicast() {
	first=${1%%:*}
	case "$first" in
		*[13579bdfBDF])
			echo "!! 组播/广播地址不可用作 BSSID: $1" >&2; return 1;;
	esac
	case "$first" in
		[026aAeE]|[026aAeE]?) ;;
		*) echo "!! 提示: $1 非本地管理地址(全局地址), 仍可用作 BSSID" >&2;;
	esac
	return 0
}

mac_to_bin() {
	echo "$1" | tr -d ':'
}

get_bssid() {
	local f=/sys/class/net/$1/address
	[ -r "$f" ] && cat "$f"
}

guest_enabled() {
	[ "$(uci get $UCI_2G.disabled 2>/dev/null)" = "0" ] && \
		[ "$(uci get $UCI_5G.disabled 2>/dev/null)" = "0" ]
}

# 四 VAP 全开布局同步 wlan_mac.bin(与驱动建 guest VAP 时一致)
sync_wlan_mac_bin() {
	local m0 m1 m2 m3
	[ -w /etc/misc/wifi/wlan_mac.bin ] || {
		echo "!! /etc/misc/wifi/wlan_mac.bin 不可写" >&2; return 1;}
	m0=$(mac_to_bin "$(uci get wireless.main_2g.macaddr)")
	m1=$(mac_to_bin "$(uci get $UCI_2G.macaddr)")
	m2=$(mac_to_bin "$(uci get wireless.main_5g.macaddr)")
	m3=$(mac_to_bin "$(uci get $UCI_5G.macaddr)")
	sed -i "s/^Intf0MacAddress=.*/Intf0MacAddress=$m0/" /etc/misc/wifi/wlan_mac.bin
	sed -i "s/^Intf1MacAddress=.*/Intf1MacAddress=$m1/" /etc/misc/wifi/wlan_mac.bin
	sed -i "s/^Intf2MacAddress=.*/Intf2MacAddress=$m2/" /etc/misc/wifi/wlan_mac.bin
	sed -i "s/^Intf3MacAddress=.*/Intf3MacAddress=$m3/" /etc/misc/wifi/wlan_mac.bin
	grep -E "^Intf[0-3]MacAddress=" /etc/misc/wifi/wlan_mac.bin
}

guest_ubus_set() {
	# $1=0 开 / 1 关
	local dis=$1 gt
	gt=$GUEST_ACTIVE_TIME
	ubus call zwrt_wlan set "{\"guest_2g\":{\"disabled\":\"$dis\",\"guest_active_time\":\"$gt\"},\"guest_5g\":{\"disabled\":\"$dis\",\"guest_active_time\":\"$gt\"}}" >/dev/null
}

wait_guest_ifaces() {
	# $1=up|down, $2=超时秒
	local want=$1 max=${2:-30} i=0
	while [ $i -lt "$max" ]; do
		if [ "$want" = up ]; then
			[ -r "/sys/class/net/$IF_2G/address" ] && \
				[ -r "/sys/class/net/$IF_5G/address" ] && return 0
		else
			[ ! -r "/sys/class/net/$IF_2G/address" ] && \
				[ ! -r "/sys/class/net/$IF_5G/address" ] && return 0
		fi
		i=$((i + 1))
		sleep 1
	done
	return 1
}

apply_guest_bssid() {
	local was_up=0
	if guest_enabled; then
		was_up=1
		echo "== 关闭 guest VAP(释放 wlan1/wlan3) =="
		guest_ubus_set 1
		wait_guest_ifaces down 20 || echo "!! guest 接口未完全消失, 继续尝试" >&2
	fi

	echo "== 同步 wlan_mac.bin (Intf0=main_2g Intf1=guest_2g Intf2=main_5g Intf3=guest_5g) =="
	sync_wlan_mac_bin

	if [ "$was_up" -eq 1 ]; then
		echo "== 重新开启 guest VAP(按新 MAC 创建) =="
		guest_ubus_set 0
		wait_guest_ifaces up 40 || echo "!! guest 接口未及时就绪, 请稍后 guest_bssid.sh status 复查" >&2
	else
		echo "== guest 当前为关闭状态: UCI/wlan_mac.bin 已更新, 启用 guest 后生效 =="
	fi
}

status() {
	echo "== 当前 guest BSSID =="
	for x in "$IF_2G:$UCI_2G" "$IF_5G:$UCI_5G"; do
		local ifn=${x%%:*} uci_sec=${x#*:}
		local rt
		rt=$(get_bssid "$ifn" || echo MISSING)
		printf "%-6s runtime=%-17s uci=%-17s ssid=%s\n" "$ifn" "$rt" \
			"$(uci get $uci_sec.macaddr 2>/dev/null)" \
			"$(uci get $uci_sec.ssid 2>/dev/null)"
	done
	echo "== guest disabled =="
	printf "guest_2g=%s guest_5g=%s\n" \
		"$(uci get $UCI_2G.disabled 2>/dev/null)" \
		"$(uci get $UCI_5G.disabled 2>/dev/null)"
	echo "== wlan_mac.bin =="
	cat /etc/misc/wifi/wlan_mac.bin 2>/dev/null
}

cmd="${1:-status}"
case "$cmd" in
	status)
		status
		exit 0
		;;
	reset)
		if [ -n "${GUEST_BSSID_RESET_2G:-}" ] && [ -n "${GUEST_BSSID_RESET_5G:-}" ]; then
			m2g=$GUEST_BSSID_RESET_2G; m5g=$GUEST_BSSID_RESET_5G
		elif [ -r "$FACTORY_FILE" ]; then
			# shellcheck disable=SC1090
			. "$FACTORY_FILE"
			m2g=${RESET_2G:-}; m5g=${RESET_5G:-}
			[ -n "$m2g" ] && [ -n "$m5g" ] || {
				echo "!! $FACTORY_FILE 需含 RESET_2G=... 与 RESET_5G=..." >&2; exit 1;}
		else
			echo "!! reset 需本机出厂 guest BSSID:" >&2
			echo "   写入 $FACTORY_FILE 或 export GUEST_BSSID_RESET_2G/5G" >&2
			exit 1
		fi
		;;
	*)
		m2g=$cmd
		shift
		m5g="${1:-}"
		;;
esac

m2g=$(norm_mac "$m2g") || { echo "!! 无效 MAC: $m2g" >&2; exit 1; }
check_unicast "$m2g" || exit 1
if [ -n "$m5g" ]; then
	m5g=$(norm_mac "$m5g") || { echo "!! 无效 MAC: $m5g" >&2; exit 1; }
	check_unicast "$m5g" || exit 1
	[ "$m2g" = "$m5g" ] && \
		echo "!! 警告: 2.4G/5G 用相同 BSSID 可能冲突" >&2
else
	m5g=$m2g
fi

[ -d /data ] && cp /etc/config/wireless "/data/wireless.bak.$(date +%Y%m%d%H%M%S)"

echo "== 设置 UCI =="
echo "$UCI_2G.macaddr -> $m2g"
echo "$UCI_5G.macaddr -> $m5g"
uci set $UCI_2G.macaddr=$m2g
uci set $UCI_5G.macaddr=$m5g
uci commit wireless

apply_guest_bssid

echo "== 生效后检查 =="
status
