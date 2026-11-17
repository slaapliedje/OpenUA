#!/usr/bin/env bash
# A repeatable play-loop profile on any Atari target, driven by the ENGINE.
#
#   make EXTRA_CFLAGS="-DFRUA_AUTOPLAY -DFRUA_AUTOWALK -DFRUA_AUTOWALK_TREASURE -DFRUA_WALKPROF"
#   make gamedata DSN=HEIRS.DSN            # same flags; stages frua.prg
#   tools/profile/walk_profile.sh tt /some/outdir
#   tools/profile/walk_aggregate.py /some/outdir/run.log WALKDUMP /some/outdir/walk.prof frua.prg
#
# (add CPU68K=68000 to both makes for `ste`.) The build's own script walks
# HEIRS's first corridor — 201 keys, half steps and half turns — and calls
# frua_prof_mark0..3 around a WALK window and a 30 s IDLE window. This script
# only arms Hatari's profiler on those four functions, so the windows are cut
# on engine ticks and three runs agree to about 1%. st_profile.sh's xdotool
# drive is paced by host time and wanders about 15%; prefer this one.
#
# Machine: tt | falcon | ste.   FRUA_XVFB_DISPLAY picks the X display (:99).
#
# ★ Kills only the emulator it started. Other sessions on the machine run
#   their own Hatari; never pkill by name here.
# ★ `pc = TEXT` alone fires in ROM before the program is loaded — hence the
#   range guard on the first breakpoint.
set -u
MACHINE="${1:?usage: walk_profile.sh tt|falcon|ste <outdir>}"
OUT="${2:?usage: walk_profile.sh tt|falcon|ste <outdir>}"
REPO=$(cd "$(dirname "$0")/../.." && pwd)
GD="${FRUA_GAMEDATA:-$REPO/data/work/gamedata}"
HATARI="${HATARI_BIN:-/usr/bin/hatari}"
XD="${FRUA_XVFB_DISPLAY:-:99}"
case "$MACHINE" in
tt)     MARGS="--machine tt --memsize 14 --dsp none --tos /usr/share/hatari/tos306us.img" ;;
falcon) MARGS="--machine falcon --memsize 14 --dsp emu --tos /usr/share/hatari/TOSv4.04.img" ;;
ste)    MARGS="--machine ste --memsize 4 --tos /usr/share/hatari/tos206us.img" ;;
*)      echo "machine must be tt, falcon or ste"; exit 2 ;;
esac
mkdir -p "$OUT"; rm -f "$OUT"/*.ini "$OUT"/*.prof "$OUT"/*.png "$OUT"/run.log
: > "$GD/DBG.LOG"

printf 'b pc = TEXT && TEXT > $1000 && TEXT < $e00000 :once :file %s/atstart.ini\n' "$OUT" > "$OUT/bp.ini"
{
	echo "symbols prg"
	for n in 0 1 2 3; do
		echo "b pc = _frua_prof_mark$n :once :file $OUT/m$n.ini"
	done
	echo "c"
} > "$OUT/atstart.ini"
printf 'screenshot %s/s0.png\nprofile on\nc\n' "$OUT" > "$OUT/m0.ini"
printf 'screenshot %s/s1.png\necho WALKDUMP\nprofile save %s/walk.prof\nc\n' "$OUT" "$OUT" > "$OUT/m1.ini"
printf 'screenshot %s/s2.png\necho IDLEOPEN\nc\n' "$OUT" > "$OUT/m2.ini"
printf 'screenshot %s/s3.png\necho IDLEDUMP\nprofile save %s/idle.prof\nquit\n' "$OUT" "$OUT" > "$OUT/m3.ini"

# shellcheck disable=SC2086
DISPLAY="$XD" SDL_VIDEODRIVER=x11 "$HATARI" $MARGS \
    --fast-forward yes --conout 2 --sound off \
    --parse "$OUT/bp.ini" -d "$GD" --auto 'C:\FRUA.PRG' \
    < /dev/null > "$OUT/run.log" 2>&1 &
HPID=$!
trap 'kill -9 $HPID 2>/dev/null' EXIT
for _ in $(seq 1 "${WALKPROF_MAXWAIT:-120}"); do
	kill -0 $HPID 2>/dev/null || break
	sleep 5
done
if kill -0 $HPID 2>/dev/null; then
	echo "TIMEOUT: the emulator never reached mark3 — run is void"
	exit 3
fi
cp "$GD/DBG.LOG" "$OUT/DBG.LOG"
for f in walk.prof idle.prof; do
	[ -s "$OUT/$f" ] || { echo "no $f — the walk never reached its marks; run is void"; exit 3; }
done
echo "done: $OUT (s0/s1 = the walk window's first and last frames)"
