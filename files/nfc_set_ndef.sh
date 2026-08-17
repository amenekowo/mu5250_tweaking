#!/bin/sh
# 操作NFC标签(FM11NT082C, /dev/i2c-3, 从机地址0x57)的NDEF消息
# 标签内存(ISO14443-A Type2 Tag): 0x00起为UID区, CC在0x0c(出厂OTP, 不可改), NDEF TLV在0x10
#
# 用法:
#   nfc_set_ndef.sh                          # 把NDEF改为URI记录, 默认 http://www.bing.com
#   nfc_set_ndef.sh <uri> [前缀码]           # 自定义URI和NFC URI前缀码
#   nfc_set_ndef.sh backup                   # 仅备份当前标签内容到/tmp, 不改动标签
#   nfc_set_ndef.sh restore <备份文件>       # 从备份文件恢复标签内容
#
# NFC URI前缀码: 0=无前缀 1=http://www. 3=http:// 4=https:// (NFC Forum URI记录定义)
#   例: nfc_set_ndef.sh www.bing.com 0   # 原样写入 "www.bing.com"
#       nfc_set_ndef.sh example.com 4    # https://example.com
#
# 注意:
#   - 写入/恢复前会停止zte_topsw_nfc守护进程, 否则手机一碰, daemon会把NDEF改回WiFi凭证
#   - 备份在 /tmp/nfc_tag_backup_*.hex, 恢复用: nfc_set_ndef.sh restore <该文件>
#   - 恢复原WiFi分享NDEF后如需重新启用: /etc/init.d/zte_topsw_nfc start

BUS=3
ADDR=0x57
TLV_BASE=0x10          # NDEF TLV起始地址(2字节大端地址, FM11NT082C)
BACKUP_BASE=0x0c       # 备份起点(CC所在block)
BACKUP_LEN=128
RETRY=8

# 写一页(最多16字节): $1=起始地址(十进制), 其余=数据字节(0x前缀)
# 注意: FM11NT082C的I2C写按16字节页回绕——写入若越过页尾会折回页首,
#       所以每次写入必须落在同一页内(地址页对齐, 或字节数不超页尾)
# 另: I2C控制器偶发ENOTCONN故障, 写后需留间隔(EEPROM写时间tWR≤10ms)并失败重试
pwrite() {
	base=$1; shift
	hi=$((base / 256)); lo=$((base % 256))
	addr_hex="0x$(printf %02x $hi) 0x$(printf %02x $lo)"
	attempt=0
	while [ $attempt -lt $RETRY ]; do
		if i2ctransfer -y $BUS w$(( $# + 2 ))@$ADDR $addr_hex "$@" 2>/dev/null; then
			sleep 0.05
			return 0
		fi
		attempt=$((attempt + 1))
		sleep 0.1
	done
	echo "!! 写入失败(地址 0x$base)" >&2
	return 1
}

# 读回: $1=起始地址(十进制), $2=字节数(默认16)
pread() {
	base=$1; len=${2:-16}
	hi=$((base / 256)); lo=$((base % 256))
	attempt=0
	while [ $attempt -lt $RETRY ]; do
		if i2ctransfer -y $BUS w2@$ADDR 0x$(printf %02x $hi) 0x$(printf %02x $lo) r$len@$ADDR 2>/dev/null; then
			return 0
		fi
		attempt=$((attempt + 1))
		sleep 0.1
	done
	echo "!! 读取失败(地址 0x$base)" >&2
	return 1
}

stop_daemon() {
	if [ -x /etc/init.d/zte_topsw_nfc ]; then
		/etc/init.d/zte_topsw_nfc stop
		echo "==> zte_topsw_nfc 已停止"
	fi
}

# 备份当前标签内容到 /tmp/nfc_tag_backup_*.hex, 路径放入全局变量 BACKUP
BACKUP=
do_backup() {
	local f b n
	f=/tmp/nfc_tag_backup_$(date +%Y%m%d_%H%M%S).hex
	if ! pread $((BACKUP_BASE)) $BACKUP_LEN > "$f"; then
		echo "备份失败: 读取标签失败"
		rm -f "$f"
		return 1
	fi
	n=0
	for b in $(cat "$f"); do
		case $b in
			0x[0-9a-fA-F][0-9a-fA-F]) n=$((n + 1)) ;;
			*) echo "备份失败($f): 无效字节 '$b'"; rm -f "$f"; return 1 ;;
		esac
	done
	if [ $n -ne $BACKUP_LEN ]; then
		echo "备份失败($f): 只读到 $n 字节"
		rm -f "$f"
		return 1
	fi
	BACKUP=$f
	echo "==> 备份: $BACKUP"
}

# 从备份文件恢复标签内容
do_restore() {
	file=$1
	if [ -z "$file" ] || [ ! -f "$file" ]; then
		echo "用法: $0 restore <备份文件>"
		return 1
	fi
	n=0; bad=0
	for b in $(cat "$file"); do
		case $b in
			0x[0-9a-fA-F][0-9a-fA-F]) n=$((n + 1)) ;;
			*) echo "备份文件格式错误: '$b'"; bad=1 ;;
		esac
	done
	[ $bad -eq 0 ] || return 1
	[ $n -gt 0 ] || { echo "备份文件为空: $file"; return 1; }
	[ $n -ne $BACKUP_LEN ] && echo "==> 警告: 备份只有 $n 字节(完整应为 $BACKUP_LEN)"

	stop_daemon
	echo "==> 从 $file 恢复($n 字节)..."
	addr=$((BACKUP_BASE))
	cnt=0
	line=
	for b in $(cat "$file"); do
		line="$line $b"
		cnt=$((cnt + 1))
		if [ $(( (addr + cnt) % 16 )) -eq 0 ]; then
			pwrite $addr $line || return 1
			addr=$((addr + cnt))
			cnt=0
			line=
		fi
	done
	if [ -n "$line" ]; then
		pwrite $addr $line || return 1
		echo "==> 警告: 末尾不足一页($cnt 字节), 未补0"
	fi
	echo "==> 回读验证:"
	pread $((BACKUP_BASE)) 48
	echo "==> 恢复完成"
}

case ${1:-} in
	backup)
		do_backup
		exit 0
		;;
	restore)
		do_restore "${2:-}"
		exit 0
		;;
esac

# ===== 写入URI NDEF =====
URI=${1:-bing.com}
PREFIX=${2:-1}

[ "$PREFIX" -gt 255 ] 2>/dev/null && { echo "前缀码错误: $PREFIX"; exit 1; }
echo "==> 目标URI: $URI (前缀码 $PREFIX)"

# 1. 停止NFC守护进程并备份(备份失败则放弃, 避免毁掉原内容)
stop_daemon
do_backup || exit 1

# 2. 组NDEF URI记录: 03 <TLV长> d1 01 <载荷长> 55 <前缀码> <uri> fe
#    TLV长 = NDEF消息总长 = 3字节头 + 类型(1) + 载荷(plen)
raw=$(printf %s "$URI" | od -An -tx1 | tr -d ' \n')
plen=$(( ${#URI} + 1 ))
tlen=$(( plen + 4 ))
tlv_hex=$(printf '03%02xd101%02x55%02x%sfe' $tlen $plen $PREFIX "$raw")
tlv_bytes=$(printf '%s' "$tlv_hex" | sed 's/../0x& /g')
nbytes=$(( ${#tlv_hex} / 2 ))
npages=$(( (nbytes + 15) / 16 ))
echo "==> NDEF TLV($nbytes字节, $npages页): $tlv_bytes"

# 3. 清掉旧数据(0x10-0x7f)并按16字节一页写入新TLV
page=$((TLV_BASE))
while [ $page -lt $((0x80)) ]; do
	pwrite $page 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 || exit 1
	page=$((page + 16))
done

page=$((TLV_BASE))
off=0
list=
for b in $tlv_bytes; do
	list="$list $b"
	off=$((off + 1))
	if [ $off -eq 16 ]; then
		pwrite $page $list || exit 1
		page=$((page + 16))
		list=; off=0
	fi
done
if [ -n "$list" ]; then
	while [ $off -lt 16 ]; do list="$list 0x00"; off=$((off + 1)); done
	pwrite $page $list || exit 1
fi

# 4. 回读验证
echo "==> 回读(0x0c起48字节):"
pread $((BACKUP_BASE)) 48
case $PREFIX in
	0) url="$URI" ;;
	1) url="http://www.$URI" ;;
	3) url="http://$URI" ;;
	4) url="https://$URI" ;;
	*) url="$URI" ;;
esac
echo "==> 完成。手机贴近标签即可打开: $url"
