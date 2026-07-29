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
#   FALCON_TOS   TOS ROM. Unset = searched, and the search is MACHINE-AWARE —
#                each machine gets the ROM it actually shipped with:
#                  --machine ste / megaste  -> TOS 2.06  (tos206us.img)
#                  --machine st  / megast   -> TOS 1.04  (tos104us.img)
#                  otherwise (Falcon)       -> TOS 4.04  (tos404.img)
#                EmuTOS is a last-resort fallback only, not a first choice.
#                Handing an ST a Falcon ROM makes hatari SILENTLY fall back to
#                Falcon mode, so an "STE" run tests the wrong backend.
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
# Falcon TOS: search, don't assume. Packagers disagree on the filename (Arch's
# `hatari` installs tos404.img; others ship TOSv4.04.img), and hardcoding one
# spelling meant every boot needed FALCON_TOS= passed by hand on a machine that
# had the ROM the whole time. Same order as the Makefile's FALCON_TOS chain.
# Only NON-EMPTY files count (a 0-byte ROM copy makes hatari print "FATAL: Can
# not load TOS", which reads as a harness bug).
#
# MACHINE-AWARE, and that part is load-bearing: this script passes
# `--machine falcon` but appends $HATARI_ARGS afterwards, so an
# `HATARI_ARGS="--machine ste"` run overrides the MACHINE and not the TOS. Hand
# an ST a Falcon ROM and hatari prints
#     ERROR: TOS version 4.04 is for Atari Falcon only.
#      ==> Switching to Falcon mode now.
# and carries on — so a run you believe is testing display_ste.c is actually
# testing display_videl.c, and nothing in the screenshots says so. That silently
# wasted a whole STE soak on 2026-07-26. Pick the ROM from the requested machine.
if [ -z "${FALCON_TOS:-}" ]; then
	# Per machine, the ROM that shipped with it — NOT one ST-ish list for all of
	# them. STE gets TOS 2.06 (the STE's own release); a plain ST gets TOS 1.04.
	# EmuTOS is the fallback only, never the first choice: it is a clean-room
	# reimplementation, so a bug that reproduces only under real TOS (or only
	# under EmuTOS) is exactly the kind we want to see rather than paper over.
	#
	# The `ste` arm MUST precede the `st` arm: "--machine st" is a substring of
	# "--machine ste", so testing st first would swallow every STE run and boot
	# it on a plain-ST ROM. The trailing space in each pattern (this case matches
	# against HATARI_ARGS padded with spaces) is what keeps them distinct.
	case " ${HATARI_ARGS:-} " in
	*"--machine ste "*|*"--machine megaste "*)
		for f in /usr/share/hatari/tos206us.img \
		         "$HOME/Downloads/Atari/tos206us.img" \
		         "$HOME/Downloads/Atari/etos256us.img" \
		         /usr/share/hatari/etos512us.img; do
			[ -s "$f" ] && { FALCON_TOS="$f"; break; }
		done
		FALCON_TOS="${FALCON_TOS:-/usr/share/hatari/tos206us.img}"
		;;
	*"--machine st "*|*"--machine megast "*)
		for f in /usr/share/hatari/tos104us.img \
		         "$HOME/Downloads/Atari/tos104us.img" \
		         /usr/share/hatari/tos106us.img \
		         /usr/share/hatari/etos512us.img; do
			[ -s "$f" ] && { FALCON_TOS="$f"; break; }
		done
		FALCON_TOS="${FALCON_TOS:-/usr/share/hatari/tos104us.img}"
		;;
	*)
		for f in /usr/share/hatari/tos404.img \
		         /usr/share/hatari/TOSv4.04.img \
		         "$HOME/Downloads/Atari/tos404.img" \
		         /usr/share/hatari/etos512us.img; do
			[ -s "$f" ] && { FALCON_TOS="$f"; break; }
		done
		# Nothing found: keep the canonical name so the error names a real path.
		FALCON_TOS="${FALCON_TOS:-/usr/share/hatari/tos404.img}"
		;;
	esac
fi
GEMDOS_DIR="${GEMDOS_DIR:-$REPO/data/work/gamedata}"
FRUA_MEM="${FRUA_MEM:-14}"
# Which Hatari binary. Default = system. Set HATARI_BIN=hrdb to enable the
# debugger: stock Hatari 2.4.1+ already speaks `hatari-debug` over the cmd-fifo
# and can load frua.prg's symbol table (`symbols prg`), so the `dbg` action
# works WITHOUT the tattlemuss fork — HATARI_BIN=hrdb just turns on symbol
# auto-load so `dbg` can reference engine symbols (_l309c, _g_a5_below, ...) by
# name. Point HRDB_HATARI at a custom build if you prefer one; it defaults to
# the system hatari.
HRDB_HATARI="${HRDB_HATARI:-$(command -v hatari)}"
HATARI_BIN="${HATARI_BIN:-hatari}"
FRUA_DBG=""
[[ "$HATARI_BIN" == hrdb ]] && { HATARI_BIN="$HRDB_HATARI"; FRUA_DBG=1; }
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
	mkdir -p "$STATE" "$STATE/shots"
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
		--screenshot-dir "$STATE/shots" \
		--crop yes \
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
	# In debug mode (HATARI_BIN=hrdb), auto-load the running program's symbol
	# table so `dbg` can reference engine names (_l309c, _g_lc_x0,
	# _g_a5_below, ...). Stock Hatari 2.4.1+ supports `symbols prg`.
	if [[ -n "$FRUA_DBG" ]]; then
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
beginplay)
	# Seat the seeded party and drop into the dungeon (the play/3D screen) —
	# the ONLY headless route to the 3D view / combat, which several mono
	# render tasks need to verify. From the main menu it runs the verified
	# flow (2026-07-19): p (Play the Game -> Training Hall) -> a (Add
	# Character -> the seeded-party list, BARBARUS highlighted) -> Return
	# (marks '* BARBARUS' = added) -> Escape (back to the hall) -> b (Begin
	# Adventuring -> dungeon). Requires a design whose start area is a level
	# >= 5 (a dungeon); TUTORIAL.DSN qualifies. Each step waits PLAY_STEP_DELAY
	# seconds (default 3 — bump to ~6 for the 8 MHz mono ST, which drops keys
	# at tighter spacing: `PLAY_STEP_DELAY=6 driver.sh beginplay`).
	WID="$(cat "$STATE/wid" 2>/dev/null)"; [[ -n "$WID" ]] || WID="$(find_window)"
	xdotool windowactivate --sync "$WID" 2>/dev/null \
		|| { xdotool windowraise "$WID" 2>/dev/null; xdotool windowfocus "$WID" 2>/dev/null; }
	d="${PLAY_STEP_DELAY:-3}"
	for step in "p:Play->Hall" "a:AddList" "Return:add-char" "Escape:back-to-hall" "b:Begin->dungeon"; do
		k="${step%%:*}"
		# The final Begin loads the dungeon art (walls + backdrop) -- slow,
		# especially on the 8 MHz mono ST -- so give it double the settle.
		sd="$d"; [[ "$k" == "b" ]] && sd="$((d * 2))"
		xdotool key "$k"
		echo "hatari_ui: beginplay ${step#*:} (key $k, wait ${sd}s)"
		sleep "$sd"
	done
	# The initial 3D-view paint after Begin can settle BLACK on the slow mono
	# ST (the walls/roster load lazily). A turn there and back (Right then Left
	# = net-zero facing, same cell) forces two full redraws so the view is
	# actually painted -- deterministic for a screenshot.
	# ★ THE NUDGE IS TWO TURN KEYS, AND IT IS NOT FREE (#97). Right+Left is
	# net-zero ONLY IF BOTH LAND. When one is dropped -- which happens
	# reproducibly on some designs -- the party is left silently rotated 90
	# degrees, and every direction measurement after it is wrong by a quarter
	# turn with nothing on screen to say so. Two such runs produced an
	# "observed = 6 - f" reflection that looked like an engine axis bug and
	# was entirely this. Set PLAY_NUDGE=0 for ANY test that reads a direction.
	if [[ "${PLAY_NUDGE:-1}" != "0" ]]; then
		xdotool key Right; sleep "$d"; xdotool key Left; sleep "$d"
		echo "hatari_ui: beginplay done -- in the dungeon, view nudged (screenshot to confirm)"
	else
		echo "hatari_ui: beginplay done -- in the dungeon, NUDGE SKIPPED (facing = authored)"
	fi
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
	# Two moves + a generous settle: Hatari's IKBD emulation rate-limits the
	# synthesized relative deltas, so after a long jump the EMULATED cursor
	# can still be mid-travel when the click fires — it then lands on
	# whatever region the path crosses (seen live: a turn-left click walked
	# the party forward through a door). The second move corrects any
	# residual drift once the first burst is consumed.
	xdotool mousemove "$sx" "$sy"
	sleep 0.4
	xdotool mousemove "$sx" "$sy"
	sleep 0.3
	xdotool click "$btn"
	sleep 0.3
	echo "hatari_ui: click $btn at window($cx,$cy) = screen($sx,$sy)"
	;;
drag)
	# Press-hold-drag-release: press at (X1,Y1), travel to (X2,Y2) in steps,
	# then release. FRUA (a Mac port) uses press-and-DRAG pulldown menus — the
	# FILE / MAP / UTILITIES bars in the GEO editor DROP only while the button
	# is held and select on release over an item; an atomic `click` (press+
	# release in place) can't drive them, but a drag can:
	#   drag <title_x> <title_y> <item_x> <item_y> [button]
	# Coordinates are screenshot pixels (window-relative, same as `click`).
	# The stepped travel + settles work around Hatari's IKBD rate-limiting of
	# synthesized relative motion (a single long jump can fire the release
	# mid-travel — see the `click` note).
	[[ $# -ge 4 ]] || die "drag needs X1 Y1 X2 Y2 (window-relative pixels)"
	x1="$1"; y1="$2"; x2="$3"; y2="$4"; btn="${5:-1}"
	WID="$(cat "$STATE/wid" 2>/dev/null)"; [[ -n "$WID" ]] || WID="$(find_window)"
	xdotool windowactivate --sync "$WID" 2>/dev/null \
		|| { xdotool windowraise "$WID" 2>/dev/null; xdotool windowfocus "$WID" 2>/dev/null; }
	eval "$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null)"
	ox="${X:-0}"; oy="${Y:-0}"
	# settle at the title, press
	xdotool mousemove "$((ox + x1))" "$((oy + y1))"; sleep 0.4
	xdotool mousemove "$((ox + x1))" "$((oy + y1))"; sleep 0.3
	xdotool mousedown "$btn"; sleep 0.5
	# travel to the item in 6 steps so the dropped menu tracks the pointer
	for i in 1 2 3 4 5 6; do
		xdotool mousemove \
			"$((ox + x1 + (x2 - x1) * i / 6))" \
			"$((oy + y1 + (y2 - y1) * i / 6))"
		sleep 0.12
	done
	xdotool mousemove "$((ox + x2))" "$((oy + y2))"; sleep 0.4
	xdotool mouseup "$btn"; sleep 0.3
	echo "hatari_ui: drag $btn ($x1,$y1)->($x2,$y2)"
	;;
dbg)
	# Drive the Hatari debugger headlessly over the command FIFO. Stock Hatari
	# 2.4.1+ speaks `hatari-debug <cmd>` over --cmd-fifo, so this works on the
	# system hatari — start with HATARI_BIN=hrdb to auto-load frua.prg's symbol
	# table (`symbols prg`). Prints the debugger's reply, captured from the
	# stdout log. With symbols loaded, reference engine names directly, e.g.:
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
dump)
	# Hatari's OWN screenshot (AltGr+G) via the control FIFO — no X grab,
	# no imagemagick, and it always captures the settled Falcon frame (cropped
	# of the statusbar via --crop). Robust in fullscreen where the X-window grab
	# breaks. Writes grabNNNN.png into $STATE/shots; we copy the newest to $out.
	out="${1:-$STATE/dump.png}"
	[[ -p "$STATE/cmd.fifo" ]] || die "no cmd.fifo — run 'start' first"
	before="$(ls -t "$STATE/shots"/*.png 2>/dev/null | head -1)"
	echo "hatari-shortcut screenshot" > "$STATE/cmd.fifo"
	for i in $(seq 1 25); do
		newest="$(ls -t "$STATE/shots"/*.png 2>/dev/null | head -1)"
		[[ -n "$newest" && "$newest" != "$before" ]] && break
		# re-send once if the first request was dropped (boot-time FIFO race)
		[[ "$i" == 8 ]] && echo "hatari-shortcut screenshot" > "$STATE/cmd.fifo"
		sleep 0.2
	done
	[[ -n "${newest:-}" && "$newest" != "$before" ]] || die "no new screenshot appeared"
	cp -f "$newest" "$out"
	echo "hatari_ui: $out (via Hatari screendump)"
	;;
log)
	cat "$LOG"
	;;
stop)
	pkill -9 -x hatari 2>/dev/null || true
	echo "hatari_ui: stopped"
	;;
quit)
	# GRACEFUL shutdown, via the command fifo. `stop` SIGKILLs, which is fine
	# for a screenshot run but truncates any file Hatari finalizes on close —
	# an --avirecord AVI keeps its RIFF/LIST sizes at 0 and is unreadable. Use
	# this whenever a recording is open. Needs --confirm-quit off (the sound
	# capture in driver.sh passes it), else Hatari waits on a dialog.
	if [[ -p "$STATE/cmd.fifo" ]]; then
		echo "hatari-shortcut quit" > "$STATE/cmd.fifo" 2>/dev/null || true
	fi
	for _ in $(seq 1 30); do
		pgrep -x hatari >/dev/null || break
		sleep 1
	done
	if pgrep -x hatari >/dev/null; then
		echo "hatari_ui: graceful quit timed out — forcing" >&2
		pkill -9 -x hatari 2>/dev/null || true
	fi
	echo "hatari_ui: quit"
	;;
*)
	die "usage: start | wait <regex> [n] [timeout] | key <keysym>... | dbg <debugger-cmd> | shot <png> | shots <png> [thresh] [tries] | dump [png] | log | quit | stop
  env: HATARI_BIN=hrdb (tattlemuss debugger fork)  READY_MARKER=<regex>|-  READY_TIMEOUT=<s>"
	;;
esac
