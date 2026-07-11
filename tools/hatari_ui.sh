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
#   FRUA_MEM     emulated ST-RAM in MB: 1, 4 or 14 (default 14 for
#                development; drop to 4/1 for the memory-fit passes)
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
FRUA_MEM="${FRUA_MEM:-14}"
# Which Hatari binary. Default = system. Set HATARI_BIN=hrdb (or a path) to use
# the tattlemuss debugger fork, which auto-loads frua.prg's symbol table so the
# `dbg` action can reference engine symbols (_l309c, _g_a5_below, ...) by name.
HRDB_HATARI="$HOME/src/tattlemuss-hatari/build/src/hatari"
HATARI_BIN="${HATARI_BIN:-hatari}"
[[ "$HATARI_BIN" == hrdb ]] && HATARI_BIN="$HRDB_HATARI"
# Readiness marker `start` waits for before dropping fast-forward. Override for
# non-menu boots (e.g. the FRUA_HALL/dungeon path emits "j200_dump: wrote", the
# merchant path emits nothing — set READY_MARKER=- to skip the wait entirely).
READY_MARKER="${READY_MARKER:-menu: modal up}"
READY_TIMEOUT="${READY_TIMEOUT:-180}"

# FRUA_NO_CONOUT=1 drops Hatari's `--conout 2` console redirect. The redirect
# is how dbg_log reaches the host terminal, but it routes BIOS device 2 to the
# host so the engine reads keys via GEMDOS Cconis/Crawcin — which DON'T surface
# non-ASCII keys (the cursor arrows). Without the redirect the engine reads via
# Bconin(2), so injected arrow keys actually reach the roster / dungeon nav.
# Trade-off: no terminal log (dbg_log's Cconws lands on Logbase, not the
# displayed triple-buffer, so the screen stays clean — screenshots still work).
# Implies READY_MARKER=- (the log has no engine markers to wait on).
if [[ -n "${FRUA_NO_CONOUT:-}" ]]; then
	CONOUT_ARG=""
	READY_MARKER="-"
else
	CONOUT_ARG="--conout 2"
fi

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

# Screenshot backend — works with ImageMagick 7 (the unified `magick`, on Arch)
# or ImageMagick 6 (`import`/`compare`, the Debian/Ubuntu/Mint default, which has
# no `magick`). im_grab <window-id> <out.png>; im_compare <a> <b> -> AE metric.
if command -v magick >/dev/null 2>&1; then
	im_grab()    { magick "x:$1" "$2" 2>/dev/null || true; }
	im_compare() { magick compare -metric AE "$1" "$2" null: 2>&1; }
else
	im_grab()    { import -window "$1" "$2" 2>/dev/null || true; }
	im_compare() { compare -metric AE "$1" "$2" null: 2>&1; }
fi

cmd="${1:-}"; shift || true
case "$cmd" in
start)
	mkdir -p "$STATE"
	pkill -9 -x hatari 2>/dev/null || true
	[[ -f "$REPO/frua.prg" ]] || die "frua.prg not built"
	: > "$LOG"
	rm -f "$STATE/cmd.fifo"      # Hatari creates the fifo itself
	[[ -x "$HATARI_BIN" || "$(command -v "$HATARI_BIN")" ]] || die "hatari binary not found: $HATARI_BIN"
	SDL_VIDEODRIVER=x11 "$HATARI_BIN" \
		--machine falcon \
		--memsize "$FRUA_MEM" \
		--dsp emu \
		--tos "$FALCON_TOS" \
		$CONOUT_ARG \
		--fast-forward yes \
		--mousewarp no \
		--joy0 none --joy1 none \
		--cmd-fifo "$STATE/cmd.fifo" \
		-d "$GEMDOS_DIR" \
		--auto 'C:\frua.prg' \
		${HATARI_ARGS:-} \
		> "$LOG" 2>&1 &
	echo $! > "$STATE/pid"
	disown
	# Wait for the readiness marker (menu_run logs "menu: modal up" when a
	# menu enters its loop). READY_MARKER=- skips the wait for boots that emit
	# no marker (the merchant event); a fixed grace period replaces it so the
	# emulator still drops out of fast-forward.
	if [[ "$READY_MARKER" == "-" ]]; then
		# Fixed grace period with fast-forward still ON, so the whole boot
		# (TOS + auto-run frua.prg + design load) completes fast before we
		# drop to real speed. The no-conout path has no engine marker to wait
		# on, so it needs a longer window than the merchant event. Override
		# with READY_GRACE.
		sleep "${READY_GRACE:-${FRUA_NO_CONOUT:+18}}" 2>/dev/null || sleep 6
	else
		wait_for "$READY_MARKER" 1 "$READY_TIMEOUT" || true
	fi
	# Drop back to real speed for interaction. The explicit option
	# form is idempotent (the fastforward shortcut TOGGLES — racy).
	echo "hatari-option --fast-forward no" > "$STATE/cmd.fifo" || true
	# On the debugger fork, auto-load the running program's symbol table so
	# `dbg` can reference engine names (_l309c, _g_lc_x0, _g_a5_below, ...).
	if [[ "$HATARI_BIN" == "$HRDB_HATARI" ]]; then
		echo "hatari-debug symbols prg" > "$STATE/cmd.fifo" || true
	fi
	find_window > /dev/null
	echo "hatari_ui: ready ($HATARI_BIN), window $(cat "$STATE/wid" 2>/dev/null)"
	;;
wait)
	[[ -n "${1:-}" ]] || die "wait needs a regex"
	wait_for "$1" "${2:-1}" "${3:-120}"
	;;
key)
	[[ $# -ge 1 ]] || die "key needs at least one keysym"
	WID="$(cat "$STATE/wid" 2>/dev/null)"; [[ -n "$WID" ]] || WID="$(find_window)"
	# SDL (Hatari) only reacts to keys delivered via XTEST to the FOCUSED
	# window; `xdotool key --window` sends SYNTHETIC events that SDL ignores
	# (this is what silently broke key injection). Focus the window — activate
	# via the WM, or raise+focus+warp the pointer when there is no WM — then
	# send each key with plain `xdotool key` (XTEST to the active window).
	xdotool windowactivate --sync "$WID" 2>/dev/null \
		|| { xdotool windowraise "$WID" 2>/dev/null; xdotool windowfocus "$WID" 2>/dev/null; }
	eval "$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null)"
	[[ -n "${WIDTH:-}" ]] && xdotool mousemove --window "$WID" \
		"$((WIDTH/2))" "$((HEIGHT/2))" 2>/dev/null || true
	for k in "$@"; do
		xdotool key "$k"
		sleep 0.3
	done
	;;
click)
	# Click a point on the Falcon display headlessly. X Y are pixels as seen
	# in a screenshot (window-relative, 1:1 with the grab). Requires the
	# launch to have set `--mousewarp no` (baked into `start` above) — with
	# mouse-warp ON, Hatari runs the host pointer in relative/grab mode and
	# absolute positioning is consumed, so clicks land nowhere. XTEST button
	# events DO register once warp is off (same mechanism as `key`). Optional
	# 3rd arg = button number (default 1). e.g. `click 150 298` = PLAY THE GAME.
	[[ $# -ge 2 ]] || die "click needs X Y (window-relative pixels, from a screenshot)"
	cx="$1"; cy="$2"; btn="${3:-1}"
	WID="$(cat "$STATE/wid" 2>/dev/null)"; [[ -n "$WID" ]] || WID="$(find_window)"
	xdotool windowactivate --sync "$WID" 2>/dev/null \
		|| { xdotool windowraise "$WID" 2>/dev/null; xdotool windowfocus "$WID" 2>/dev/null; }
	eval "$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null)"
	sx=$((${X:-0} + cx)); sy=$((${Y:-0} + cy))
	xdotool mousemove "$sx" "$sy"
	sleep 0.2
	xdotool click "$btn"
	sleep 0.3
	echo "hatari_ui: click $btn at window($cx,$cy) = screen($sx,$sy)"
	;;
dbg)
	# Drive the Hatari debugger headlessly over the command FIFO (needs the
	# tattlemuss fork: HATARI_BIN=hrdb). Sends `hatari-debug <cmd>` and prints
	# the debugger's reply, captured from the conout log. frua.prg symbols are
	# auto-loaded, so reference engine names directly, e.g.:
	#   tools/hatari_ui.sh dbg 'm _g_lc_x0 _g_lc_y0'   # last wall-blit origin
	#   tools/hatari_ui.sh dbg 'b _l309c'              # break on the wall blit
	#   tools/hatari_ui.sh dbg 'm _g_a5_below+12288'   # A5 globals (party cell)
	[[ $# -ge 1 ]] || die "dbg needs a debugger command (e.g. 'm _g_lc_x0')"
	[[ -e "$STATE/cmd.fifo" ]] || die "no cmd.fifo — run 'start' with HATARI_BIN=hrdb first"
	dbg_before=$(wc -l < "$LOG" 2>/dev/null || echo 0)
	echo "hatari-debug $*" > "$STATE/cmd.fifo"
	sleep 0.6
	tail -n "+$((dbg_before + 1))" "$LOG" 2>/dev/null
	;;
shot)
	OUT="${1:-/tmp/frua-shot.png}"
	WID="$(cat "$STATE/wid" 2>/dev/null)" || WID="$(find_window)"
	# Retry: a ~358-byte PNG is an empty grab.
	for _ in 1 2 3; do
		im_grab "$WID" "$OUT"
		[[ -f "$OUT" && "$(stat -c%s "$OUT")" -gt 2000 ]] && break
		sleep 0.5
	done
	[[ -f "$OUT" ]] || die "capture failed"
	echo "hatari_ui: $OUT ($(stat -c%s "$OUT") bytes)"
	;;
shots)
	# Stable-frame screenshot: the Falcon play screen does a slow full-screen
	# c2p present, so a plain `shot` often catches a half-drawn frame (the
	# "garbage"/black-viewport captures that look like crashes but aren't).
	# Grab repeatedly until two consecutive frames settle (pixel diff below
	# THRESH), so we only save a fully-rendered frame. Falls back to the last
	# grab on timeout. Usage: shots <out.png> [thresh=200] [maxtries=30]
	OUT="${1:-/tmp/frua-shot.png}"
	THRESH="${2:-200}"
	TRIES="${3:-30}"
	WID="$(cat "$STATE/wid" 2>/dev/null)" || WID="$(find_window)"
	PREV="$STATE/shotprev.png"
	CUR="$STATE/shotcur.png"
	im_grab "$WID" "$PREV"
	stable=0
	for _ in $(seq 1 "$TRIES"); do
		sleep 0.4
		im_grab "$WID" "$CUR"
		[[ -f "$CUR" && "$(stat -c%s "$CUR")" -gt 2000 ]] || continue
		d="$(im_compare "$PREV" "$CUR" \
		     | grep -oE '^[0-9]+' | head -1 || echo 999999)"
		cp "$CUR" "$PREV"
		if [[ "${d:-999999}" -lt "$THRESH" ]]; then
			stable=1
			break
		fi
	done
	cp "$CUR" "$OUT" 2>/dev/null || cp "$PREV" "$OUT"
	echo "hatari_ui: $OUT ($(stat -c%s "$OUT") bytes, stable=$stable)"
	;;
log)
	cat "$LOG"
	;;
stop)
	pkill -9 -x hatari 2>/dev/null || true
	echo "hatari_ui: stopped"
	;;
*)
	die "usage: start | wait <regex> [n] [timeout] | key <keysym>... | dbg <debugger-cmd> | shot <png> | shots <png> [thresh] [tries] | log | stop
  env: HATARI_BIN=hrdb (tattlemuss debugger fork)  READY_MARKER=<regex>|-  READY_TIMEOUT=<s>"
	;;
esac
