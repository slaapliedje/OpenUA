#!/usr/bin/env bash
# Cycle-accurate play-loop profile of the ST/STe build, via Hatari's CPU profiler.
#
# ★ WHY THIS EXISTS: driving `profile` over the command FIFO SILENTLY COLLECTS
# NOTHING. `hatari-debug <cmd>` executes out-of-band WITHOUT entering the
# debugger, and Hatari commits its profile working set only on a real debugger
# entry ("Data is collected until debugger is entered again" — `help profile`).
# Every query that way returns "0 CPU addresses listed" while still reporting a
# plausible total time, which reads exactly like a broken build. It is not: the
# same binary profiles correctly the moment a BREAKPOINT does the entry.
# Verified by profiling a bare TOS boot, which shows activity in ROM and none
# in RAM — the right answer.
#
# Breakpoints must also come from --parse or a breakpoint's own :file script;
# `b` over the FIFO does not register either. So the whole window is expressed
# as two pre-armed breakpoints with attached command files:
#
#   VBL > $OPEN   :once :file  -> profile on ; c      (window opens)
#   VBL > $CLOSE  :once :file  -> profile cycles N ; c (window closes + dumps)
#
# Usage:  tools/profile/st_profile.sh [open_vbl] [close_vbl] [top_n]
# Then:   tools/profile/st_aggregate.py <logfile>
set -u
OPEN="${1:-100000}"; CLOSE="${2:-130000}"; TOPN="${3:-400}"
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${FRUA_PROF_OUT:-/tmp/frua-stprof}"
HATARI="${HATARI_BIN:-$HOME/opt/hatari/bin/hatari}"
TOS="${ST_TOS:-/usr/share/hatari/tos206us.img}"
mkdir -p "$OUT"; rm -f "$OUT"/cmd.fifo "$OUT"/run.log

# ★ LEAVE THE --cmd-fifo ALONE: Hatari creates it and reads it NON-BLOCKING.
# Do not pre-create it (`mkfifo` first = usage error, the emulator prints its
# help and exits), and do NOT hold the write end open to be helpful — with a
# writer attached and no commands to read, Hatari spins on EAGAIN and fills the
# log with "command FIFO read error: Resource temporarily unavailable" instead of
# emulating (13,348 lines of it, and no engine output at all).
#
# ★ AND IF THE EMULATOR NEVER STARTS, SUSPECT THE DISPLAY, NOT THIS FILE. A
# wedged Xvfb — pid and socket alive, answering nothing — leaves Hatari in poll()
# with 00:00:00 of cpu time and a one-line log, which looks precisely like the
# engine hanging on boot. `ps -o time= -C hatari` tells them apart at a glance.
# Recipe for a clean one: setsid Xvfb :97 -screen 0 1280x1024x24 </dev/null &

printf 'profile on\nc\n'                    > "$OUT/open.ini"
printf 'profile cycles %s\nc\n' "$TOPN"     > "$OUT/close.ini"
printf 'b VBL > %s :once :file %s/open.ini\nb VBL > %s :once :file %s/close.ini\n' \
       "$OPEN" "$OUT" "$CLOSE" "$OUT" > "$OUT/bp.ini"

XD="${FRUA_XVFB_DISPLAY:-:99}"
pkill -9 -x hatari 2>/dev/null; sleep 1
( DISPLAY="$XD" SDL_VIDEODRIVER=x11 "$HATARI" \
    --machine ste --memsize 4 --tos "$TOS" --fast-forward yes --conout 2 \
    --cmd-fifo "$OUT/cmd.fifo" --parse "$OUT/bp.ini" \
    -d "$REPO/data/work/gamedata" --auto 'C:\FRUA.PRG' \
    < /dev/null > "$OUT/run.log" 2>&1 & )
trap 'pkill -TERM -x hatari 2>/dev/null' EXIT

find_wid() {
	DISPLAY="$XD" xwininfo -root -tree 2>/dev/null \
	  | grep -i hatari | head -1 | awk '{print $1}'
}
send() { DISPLAY="$XD" xdotool key --window "$1" "$2" 2>/dev/null; }

# ★ THE WINDOW MUST NOT OPEN BEFORE WE ARE IN THE DUNGEON. This script used to
# boot and go straight to pressing arrows, which only reached the walk if a
# data/work/gamedata/autoload.dat happened to resume a save. There is no
# autoload.dat now, so as written it profiled the MAIN MENU and labelled the
# result "play loop" — arrow keys at the menu just move a selection. Drive the
# verified headless route to the dungeon first (hatari_ui.sh `beginplay`:
# p -> a -> Return -> Escape -> b), and assert afterwards that the profile
# really contains 3D work.
echo "waiting for the menu ..."
for _ in $(seq 1 60); do
	grep -q 'menu: modal up' "$OUT/run.log" 2>/dev/null && break
	sleep 5
done
grep -q 'menu: modal up' "$OUT/run.log" 2>/dev/null \
  || echo "WARNING: never saw 'menu: modal up' — the boot may have stalled"

WID=$(find_wid)
[ -n "$WID" ] || { echo "no Hatari window on $XD"; exit 1; }
echo "menu up — entering the dungeon (beginplay)"
D="${PLAY_STEP_DELAY:-3}"
for step in p a Return Escape b; do
	send "$WID" "$step"
	# the final Begin loads the dungeon art — give it double the settle
	[ "$step" = b ] && sleep $((D * 2)) || sleep "$D"
done
sleep "$D"

# Only NOW should the profile window be allowed to open. If it opened while we
# were still walking through the menus, the window is contaminated and the run
# is void — say so rather than aggregating it.
if grep -q "VBL=$((OPEN+1))" "$OUT/run.log" 2>/dev/null; then
	echo "VOID: the profile window opened during setup (VBL > $OPEN came too early)."
	echo "      Raise open_vbl — e.g. $0 $((OPEN * 2)) $((CLOSE * 2)) $TOPN"
	pkill -9 -x hatari 2>/dev/null
	exit 2
fi

echo "in the dungeon — waiting for the window to open (VBL > $OPEN) ..."
until grep -q "VBL=$((OPEN+1))" "$OUT/run.log" 2>/dev/null; do sleep 5; done
echo "window OPEN — driving the walk"
for k in Up Left Up Right Up Down Up Left Up Right Up Left Up Right Up Down; do
	send "$WID" "$k"
	sleep 0.7
done
echo "walked — waiting for the window to close (VBL > $CLOSE) ..."
until grep -q "CPU addresses listed" "$OUT/run.log" 2>/dev/null; do sleep 5; done
pkill -9 -x hatari 2>/dev/null
echo "done: $OUT/run.log"
echo
"$(dirname "$0")/st_aggregate.py" "$OUT/run.log" "$REPO/frua.prg" || true
