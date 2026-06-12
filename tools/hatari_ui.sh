#!/usr/bin/env bash
#
# Interactive Hatari UI harness — launch frua.prg, wait for log
# markers instead of fixed sleeps, drive keys, grab screenshots.
#
# The old workflow slept a fixed ~110 s before every interaction;
# this polls the console log twice a second and returns the moment
# the engine reports readiness, so a menu-up round trip is bounded
# by the actual boot time, not a guess.
#
# Usage:
#   tools/hatari_ui.sh start            # boot; returns when the menu is up
#   tools/hatari_ui.sh wait 'regex' [n] # block until regex has >= n hits (def 1)
#   tools/hatari_ui.sh key  <keysym>... # send key(s) to the Hatari window
#   tools/hatari_ui.sh shot <out.png>   # screenshot the Falcon display
#   tools/hatari_ui.sh log              # print the console log so far
#   tools/hatari_ui.sh stop             # kill Hatari
#
# State lives in /tmp/frua-ui (log, pid, window id).
#
# Environment:
#   FALCON_TOS   Falcon TOS ROM (default /usr/share/hatari/TOSv4.04.img)
#   GEMDOS_DIR   GEMDOS C: mount (default data/work/gamedata)
#   HATARI_ARGS  extra Hatari args
#
# Readiness marker: menu_run logs "menu: modal up" when a menu screen
# enters its event loop. `start` boots with fast-forward ON, waits for
# that marker, then toggles fast-forward OFF through Hatari's command
# FIFO so the screen runs at real speed for interaction.

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
STATE=/tmp/frua-ui
LOG="$STATE/conout.log"
FALCON_TOS="${FALCON_TOS:-/usr/share/hatari/TOSv4.04.img}"
GEMDOS_DIR="${GEMDOS_DIR:-$REPO/data/work/gamedata}"

die() { echo "hatari_ui: $*" >&2; exit 1; }

find_window() {
	# The child window with class "hatari", not the WM frame parent.
	local wid
	for _ in $(seq 1 20); do
		wid="$(xwininfo -root -tree 2>/dev/null \
			| grep -i '("hatari" "hatari")' \
			| head -1 | awk '{print $1}')" || true
		if [[ -n "${wid:-}" ]]; then
			echo "$wid" > "$STATE/wid"
			echo "$wid"
			return 0
		fi
		sleep 0.5
	done
	die "no Hatari window found"
}

wait_for() {
	# wait_for <regex> <min-hits> <timeout-s>
	local regex="$1" need="${2:-1}" tmo="${3:-120}" i hits
	for ((i = 0; i < tmo * 2; i++)); do
		hits="$(grep -cE "$regex" "$LOG" 2>/dev/null)" || hits=0
		if (( hits >= need )); then
			echo "hatari_ui: '$regex' x$hits after $((i / 2))s"
			return 0
		fi
		sleep 0.5
	done
	die "timeout (${tmo}s) waiting for '$regex' x$need; log tail:
$(tail -5 "$LOG" 2>/dev/null)"
}

cmd="${1:-}"; shift || true
case "$cmd" in
start)
	mkdir -p "$STATE"
	pkill -9 -x hatari 2>/dev/null || true
	[[ -f "$REPO/frua.prg" ]] || die "frua.prg not built"
	: > "$LOG"
	rm -f "$STATE/cmd.fifo"      # Hatari creates the fifo itself
	SDL_VIDEODRIVER=x11 hatari \
		--machine falcon \
		--dsp emu \
		--tos "$FALCON_TOS" \
		--conout 2 \
		--fast-forward yes \
		--cmd-fifo "$STATE/cmd.fifo" \
		-d "$GEMDOS_DIR" \
		--auto 'C:\frua.prg' \
		${HATARI_ARGS:-} \
		> "$LOG" 2>&1 &
	echo $! > "$STATE/pid"
	disown
	# menu_run logs this when the menu enters its event loop.
	wait_for 'menu: modal up' 1 180
	# Drop back to real speed for interaction. The explicit option
	# form is idempotent (the fastforward shortcut TOGGLES — racy).
	echo "hatari-option --fast-forward no" > "$STATE/cmd.fifo" || true
	find_window > /dev/null
	echo "hatari_ui: menu up, window $(cat "$STATE/wid")"
	;;
wait)
	[[ -n "${1:-}" ]] || die "wait needs a regex"
	wait_for "$1" "${2:-1}" "${3:-120}"
	;;
key)
	[[ $# -ge 1 ]] || die "key needs at least one keysym"
	WID="$(cat "$STATE/wid" 2>/dev/null)" || WID="$(find_window)"
	xdotool windowactivate --sync "$WID"
	for k in "$@"; do
		xdotool key --window "$WID" "$k"
		sleep 0.2
	done
	;;
shot)
	OUT="${1:-/tmp/frua-shot.png}"
	WID="$(cat "$STATE/wid" 2>/dev/null)" || WID="$(find_window)"
	# Retry: a ~358-byte PNG is an empty grab.
	for _ in 1 2 3; do
		magick "x:$WID" "$OUT" 2>/dev/null || true
		[[ -f "$OUT" && "$(stat -c%s "$OUT")" -gt 2000 ]] && break
		sleep 0.5
	done
	[[ -f "$OUT" ]] || die "capture failed"
	echo "hatari_ui: $OUT ($(stat -c%s "$OUT") bytes)"
	;;
log)
	cat "$LOG"
	;;
stop)
	pkill -9 -x hatari 2>/dev/null || true
	echo "hatari_ui: stopped"
	;;
*)
	die "usage: start | wait <regex> [n] [timeout] | key <keysym>... | shot <png> | log | stop"
	;;
esac
