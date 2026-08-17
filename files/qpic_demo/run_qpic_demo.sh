#!/bin/sh
# Run QPIC DRM demo on device: stop UI → demo → restore UI.
# Usage: sh run_qpic_demo.sh [clock_seconds]
# Deploy: scp/adb push this script + qpic_drm_demo to /tmp or /data

set -eu

LOG=/root/qpic_demo.log
log() {
	echo "$(date '+%H:%M:%S.%3N') [$0] $*" >> "$LOG"
	# also mirror to stderr for live SSH
	echo "[run] $*" >&2
}

CLOCK_SECS="${1:-12}"
LCD_BL="${LCD_BL:-/sys/class/leds/led:lcd/brightness}"
LCD_BL_SAVED=""

save_lcd_brightness() {
	if [ -r "$LCD_BL" ]; then
		LCD_BL_SAVED="$(cat "$LCD_BL" 2>/dev/null || echo 0)"
		log "lcd brightness was ${LCD_BL_SAVED}"
	fi
}

set_lcd_brightness() {
	local val="$1"
	if [ -w "$LCD_BL" ]; then
		echo "$val" > "$LCD_BL" 2>/dev/null || true
		log "lcd brightness -> ${val} (now $(cat "$LCD_BL" 2>/dev/null || echo ?))"
	fi
}

restore_lcd_brightness() {
	if [ -n "$LCD_BL_SAVED" ] && [ -w "$LCD_BL" ]; then
		echo "$LCD_BL_SAVED" > "$LCD_BL" 2>/dev/null || true
		log "lcd brightness restored to ${LCD_BL_SAVED}"
	fi
}

log "=== run_qpic_demo.sh start (clock=${CLOCK_SECS}) ==="
# snapshot kernel ring buffer before display takeover (best-effort)
( dmesg -c > /root/qpic_dmesg_before.log 2>/dev/null || true )

DEMO_BIN="$(dirname "$0")/qpic_drm_demo"
if [ ! -x "$DEMO_BIN" ]; then
	DEMO_BIN="/data/qpic_demo/qpic_drm_demo"
fi
if [ ! -x "$DEMO_BIN" ]; then
	DEMO_BIN="/tmp/qpic_drm_demo"
fi
if [ ! -x "$DEMO_BIN" ]; then
	log "ERROR: qpic_drm_demo not found"
	echo "qpic_drm_demo not found next to this script, /data/qpic_demo/, or /tmp/"
	exit 1
fi

restore_ui() {
	log "restore zte_topsw_devui"
	restore_lcd_brightness
	/etc/init.d/zte_topsw_devui start 2>/dev/null || true
	# mtdev2tuio is normally started with UI stack; startdui may be enough via procd
	if ! pidof mtdev2tuio >/dev/null 2>&1; then
		if [ -x /usr/bin/mtdev2tuio ]; then
			mtdev2tuio /dev/input/event3 osc.udp://127.0.0.1:3333/ >/dev/null 2>&1 &
		fi
	fi
}

trap restore_ui EXIT INT TERM

echo "==> stop UI"
log "stopping UI"
/etc/init.d/zte_topsw_devui stop 2>/dev/null || true
killall -9 zte_topsw_devui mtdev2tuio 2>/dev/null || true
sleep 2
if pidof zte_topsw_devui >/dev/null 2>&1; then
	log "WARN: zte_topsw_devui still running, kill again"
	killall -9 zte_topsw_devui 2>/dev/null || true
	sleep 1
fi
log "UI stopped"

save_lcd_brightness
# devui exit often leaves backlight at 0; DRM frames are invisible without it
set_lcd_brightness 255

echo "==> run $DEMO_BIN (clock ${CLOCK_SECS}s)"
log "running $DEMO_BIN (clock ${CLOCK_SECS}s)"
"$DEMO_BIN" "$CLOCK_SECS"
log "demo finished, exit=$?"
echo "==> demo finished"
