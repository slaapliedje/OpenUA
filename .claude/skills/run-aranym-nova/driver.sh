#!/usr/bin/env bash
# ARAnyM Nova-path harness: boots OpenUA on ARAnyM (EmuTOS + BetaDOS/hostfs +
# fVDI 8bpp 640x400) so the NOVA display backend runs without hardware.
# See SKILL.md for what this can and cannot verify.
set -u
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
S="${FRUA_ARANYM_STATE:-/tmp/frua-aranym}"
DISP="${FRUA_ARANYM_DISPLAY:-:96}"
C="$S/drive_c"
mkdir -p "$S"

case "${1:-}" in
build)
	# Refresh the engine + cfg in the hostfs drive (gamedata must already
	# be staged there — `setup` does the full assembly).
	cp "$REPO/frua.prg" "$C/frua.prg" && cp "$REPO/frua.rsc" "$C/frua.rsc" 2>/dev/null
	printf 'nova=force\n' > "$C/video.cfg"
	echo "staged $REPO/frua.prg -> $C"
	;;
start)
	xdpyinfo -display "$DISP" >/dev/null 2>&1 || { Xvfb "$DISP" -screen 0 1280x1024x24 >/dev/null 2>&1 & sleep 2; }
	rm -f "$C/dbg.log" "$C/DBG.LOG"
	DISPLAY="$DISP" SDL_VIDEODRIVER=x11 aranym -c "$S/aranym.cfg" > "$S/aranym.log" 2>&1 &
	echo $! > "$S/pid"
	for i in $(seq 60); do grep -q 'menu: modal up' "$C/dbg.log" 2>/dev/null && { echo "menu up after ~$((i*3))s"; exit 0; }; sleep 3; done
	echo "TIMEOUT waiting for 'menu: modal up' — tail of dbg.log:"; tail -5 "$C/dbg.log" 2>/dev/null; tail -3 "$S/aranym.log"; exit 1
	;;
log)  cat "$C/dbg.log" 2>/dev/null ;;
wait) shift; pat="${1:?regex}"; n="${2:-1}"; for i in $(seq 100); do [ "$(grep -cE "$pat" "$C/dbg.log" 2>/dev/null)" -ge "$n" ] && exit 0; sleep 3; done; echo "wait '$pat' timed out"; exit 1 ;;
shot) shift; DISPLAY="$DISP" import -window root "${1:?out.png}" ;;
stop) pkill -x aranym 2>/dev/null; echo stopped ;;
*) echo "usage: $0 build|start|log|wait <re> [n]|shot <png>|stop"; exit 2 ;;
esac
