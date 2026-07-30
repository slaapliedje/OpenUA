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

printf 'profile on\nc\n'                    > "$OUT/open.ini"
printf 'profile cycles %s\nc\n' "$TOPN"     > "$OUT/close.ini"
printf 'b VBL > %s :once :file %s/open.ini\nb VBL > %s :once :file %s/close.ini\n' \
       "$OPEN" "$OUT" "$CLOSE" "$OUT" > "$OUT/bp.ini"

pkill -9 -x hatari 2>/dev/null; sleep 1
( DISPLAY="${FRUA_XVFB_DISPLAY:-:99}" SDL_VIDEODRIVER=x11 "$HATARI" \
    --machine ste --memsize 4 --tos "$TOS" --fast-forward yes --conout 2 \
    --cmd-fifo "$OUT/cmd.fifo" --parse "$OUT/bp.ini" \
    -d "$REPO/data/work/gamedata" --auto 'C:\FRUA.PRG' > "$OUT/run.log" 2>&1 & )

echo "waiting for the window to open (VBL > $OPEN) ..."
until grep -q "VBL=$((OPEN+1))" "$OUT/run.log" 2>/dev/null; do sleep 5; done
echo "window OPEN — driving the walk"
WID=$(DISPLAY="${FRUA_XVFB_DISPLAY:-:99}" xwininfo -root -tree 2>/dev/null \
      | grep -i hatari | head -1 | awk '{print $1}')
for k in Up Left Up Right Up Down Up Left Up Right Up Left Up Right Up Down; do
	DISPLAY="${FRUA_XVFB_DISPLAY:-:99}" xdotool key --window "$WID" "$k" 2>/dev/null
	sleep 0.7
done
echo "walked — waiting for the window to close (VBL > $CLOSE) ..."
until grep -q "CPU addresses listed" "$OUT/run.log" 2>/dev/null; do sleep 5; done
pkill -9 -x hatari 2>/dev/null
echo "done: $OUT/run.log"
