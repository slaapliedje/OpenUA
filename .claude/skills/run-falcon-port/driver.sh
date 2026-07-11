#!/usr/bin/env bash
#
# Headless driver for the FRUA Falcon030/TT030 port.
#
# Builds frua.prg (m68k-atari-mint cross-GCC), boots it in the Hatari Falcon
# emulator under a virtual X server (Xvfb), screenshots the Falcon display, and
# sends keystrokes. It is a THIN wrapper over the project's real harness,
# tools/hatari_ui.sh — it adds the one thing a headless container lacks: a
# DISPLAY. On a box that already has X (DISPLAY set + reachable), it passes
# straight through to that server.
#
# Usage (run from anywhere; paths resolve to the repo root):
#   driver.sh build [make-args]   # cross-compile frua.prg + frua.rsc
#   driver.sh start               # ensure Xvfb, boot, wait for "menu: modal up"
#   driver.sh shot   <out.png>    # single screenshot of the Falcon display
#   driver.sh shots  <out.png>    # STABLE-frame screenshot (use for the play/dungeon screen)
#   driver.sh key    <keysym>...  # send key(s) via XTEST
#   driver.sh click  <x> <y> [b]  # click the Falcon display at screenshot pixel (x,y) — mouse-warp off makes this work
#   driver.sh wait   <regex> [n]  # block until the conout log has >= n matches
#   driver.sh log                 # dump the conout log
#   driver.sh stop                # kill Hatari (leaves Xvfb up for reuse)
#   driver.sh smoke  <out.png>    # build -> start -> shots -> stop, in one call
#
# Env:
#   DISPLAY               reuse an existing X server instead of Xvfb
#   FRUA_XVFB_DISPLAY     Xvfb display to spawn when headless (default :99)
#   GEMDOS_DIR            game-data C: mount (default data/work/gamedata)
#   FALCON_TOS            Falcon TOS ROM (default /usr/share/hatari/TOSv4.04.img)
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# .claude/skills/run-falcon-port  ->  repo root is three levels up.
REPO="$(cd "$HERE/../../.." && pwd)"
UI="$REPO/tools/hatari_ui.sh"
XDISP="${FRUA_XVFB_DISPLAY:-:99}"

ensure_display() {
	# A usable DISPLAY already? keep it.
	if [[ -n "${DISPLAY:-}" ]] && xdpyinfo >/dev/null 2>&1; then
		return
	fi
	# Bring up (or reuse) a persistent headless Xvfb. nohup+disown so it
	# outlives this shell — Hatari, screenshots and key-sends across separate
	# driver invocations all attach to the same server.
	if ! xdpyinfo -display "$XDISP" >/dev/null 2>&1; then
		nohup Xvfb "$XDISP" -screen 0 1280x1024x24 >/tmp/frua-xvfb.log 2>&1 &
		disown
		for _ in $(seq 1 20); do
			xdpyinfo -display "$XDISP" >/dev/null 2>&1 && break
			sleep 0.3
		done
	fi
	export DISPLAY="$XDISP"
	xdpyinfo >/dev/null 2>&1 || { echo "driver: could not start Xvfb on $XDISP (see /tmp/frua-xvfb.log)" >&2; exit 1; }
}

cmd="${1:-}"; shift || true
case "$cmd" in
	build)
		cd "$REPO"; exec make "$@"
		;;
	start|shot|shots|dump|key|click|wait|dbg|log)
		ensure_display
		exec "$UI" "$cmd" "$@"
		;;
	stop)
		# Kill only the emulator (exact name match); leave Xvfb up for reuse.
		exec "$UI" stop
		;;
	smoke)
		OUT="${1:-/tmp/frua-smoke.png}"
		cd "$REPO"
		make
		ensure_display
		"$UI" start
		"$UI" shots "$OUT"
		"$UI" stop
		echo "driver: smoke screenshot -> $OUT"
		;;
	*)
		echo "usage: $(basename "$0") build | start | shot <png> | shots <png> | key <keysym>... | click <x> <y> [button] | wait <regex> [n] | log | stop | smoke <png>" >&2
		exit 1
		;;
esac
