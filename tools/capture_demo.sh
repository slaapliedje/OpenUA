#!/usr/bin/env bash
#
# capture_demo.sh — record the same short playthrough on every target, at the
# emulated machine's own speed, so the five videos can be compared side by side.
#
#   tools/capture_demo.sh falcon|tt|ste|aga|ecs [outdir]
#
# The run is the FRUA_AUTOWALK_INN script (platform/input.c): main menu -> add a
# character -> begin adventuring -> sit through the Skull Crag caravan's entry
# chain -> take the hoard -> walk to the door of 'The Thirsty Traveler' and turn
# in. Same keys, same order, same design (HEIRS.DSN) on all five.
#
# ★ WHY REAL SPEED IS NOT OPTIONAL HERE, TWICE OVER.
#
# 1. It is the whole point. The capture exists to show how long a redraw takes;
#    an emulator running fast-forward answers a different question.
# 2. Fast-forward also breaks the SCRIPT. With hatari_ui.sh's default (boot
#    under --fast-forward, drop it at the menu marker) the run reaches the walk
#    bar and then ends in the dungeon EDITOR — reproducible, and reproducible
#    with the proven unscaled delays too, so it is not a pacing artefact. With
#    --fast-forward no from launch the same build walks the route correctly.
#    Whatever the mechanism, do not "optimise" the boot by putting it back.
#
# Timing fidelity differs between the two emulators, and the difference matters
# when reading the output:
#
#   Hatari  records one AVI frame per emulated VBL, so the file's timeline IS
#           the emulated machine's timeline. If the host cannot sustain real
#           time the recording still plays back correctly — it just took longer
#           to make. Wall-clock and video length will not agree; that is fine.
#   amiberry has no such recorder, so the Amiga runs are screen-grabbed off X11
#           in HOST time. That is only honest while amiberry holds real time, so
#           the config must say cpu_speed=real (NOT max — openua-ecs.uae ships
#           max) and the run reports its own wall/emulated ratio for checking.
#
set -euo pipefail

TARGET="${1:?usage: capture_demo.sh falcon|tt|ste|aga|ecs [outdir]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${2:-$REPO/data/work/capture}"
DEMO_FLAGS='-DFRUA_AUTOPLAY -DFRUA_AUTOWALK -DFRUA_AUTOWALK_TREASURE -DFRUA_AUTOWALK_INN'
mkdir -p "$OUTDIR"
RAW="$OUTDIR/$TARGET-raw.avi"

say() { echo "capture[$TARGET]: $*"; }

# ---------------------------------------------------------------- Atari (Hatari)
capture_atari() {
	local machine="$1"; shift
	local makeargs=("$@")

	say "building ${makeargs[*]}"
	cd "$REPO"
	rm -f platform/input.o
	make "${makeargs[@]}" EXTRA_CFLAGS="$DEMO_FLAGS" >/dev/null

	pkill -9 -x hatari 2>/dev/null || true
	rm -f "$RAW"
	local t0 t1 t2
	t0=$(date +%s)
	# env -u DISPLAY: an inherited DISPLAY=:0 puts Hatari on the user's real
	# desktop, where it also swallows their keystrokes. The driver brings up
	# its own Xvfb.
	# --borders no: on the ST/STe the 320x200 screen sits inside a wide overscan
	# border that Hatari renders in palette entry 0 — and entry 0 is one of the
	# sixteen the engine re-bands per screen, so the border changes colour every
	# time the quantiser runs. Authentic, but on a side-by-side it reads as the
	# whole frame flashing green/blue/brown while the Falcon sits still, which is
	# an artefact of the frame the game does not draw. The 020 machines have no
	# such border, so this only affects the ST arm.
	env -u DISPLAY \
	  HATARI_ARGS="--machine $machine --borders no --confirm-quit off --avirecord --avi-file $RAW --avi-vcodec png --png-level 1 --fast-forward no" \
	  READY_TIMEOUT=600 \
	  "$REPO/.claude/skills/run-falcon-port/driver.sh" start
	t1=$(date +%s)
	say "boot to main menu: $((t1-t0))s wall"

	# The engine's own end marker, out of the GEMDOS-mounted DBG.LOG — the same
	# shape as the Amiga arm below, and for a reason that matters to the OUTPUT:
	# the console sink (dbg_log/Cconws) is drawn by TOS's console driver
	# straight into screen memory, so a marker on the console would leave a band
	# of coloured fragments across the final frames of every capture.
	local dbg="${GEMDOS_DIR:-$REPO/data/work/gamedata}/DBG.LOG"
	local waited=0
	while ! grep -q 'autoplay: script done' "$dbg" 2>/dev/null; do
		sleep 5; waited=$((waited+5))
		[[ $waited -gt 900 ]] && { say "TIMED OUT waiting for the script"; break; }
	done
	t2=$(date +%s)
	say "playthrough: $((t2-t1))s wall"
	# ★ The marker fires when the LAST KEY IS SENT, not when its redraw lands —
	# and the last key is the step into the tavern, whose whole point is the
	# picture it paints. Five seconds was enough on the Falcon and cut the STe
	# off mid-load: its final frame was an empty black viewport where the other
	# machines show the barmaid. Give the slowest target room.
	sleep "${CAPTURE_TAIL:-30}"
	# GRACEFUL quit, not stop: a SIGKILL leaves the AVI's RIFF/LIST sizes at 0
	# and the file unreadable.
	env -u DISPLAY "$REPO/tools/hatari_ui.sh" quit
}

# ---------------------------------------------------------------- Amiga (amiberry)
capture_amiga() {
	local conf="$1"; shift
	local makeargs=("$@")
	local D="${FRUA_AMIGA_DISPLAY:-:99}"

	say "building ${makeargs[*]}"
	cd "$REPO"
	rm -f platform/input.o
	make "${makeargs[@]}" EXTRA_CFLAGS="$DEMO_FLAGS ${EXTRA_DEFS:-}" >/dev/null
	cp frua data/work/amiga-mount/frua
	rm -f data/work/amiga-mount/DBG.LOG

	pkill -x amiberry 2>/dev/null || true
	rm -f "$RAW"
	local t0 t1
	t0=$(date +%s)
	# ★ This wait is ONLY long enough for the amiberry window to exist, and must
	# NOT be long enough for the machine to boot. The driver's `boot` sleeps
	# before it looks for the window, and the grab cannot start until it
	# returns — so a boot-length wait (the driver's own 150 s default, sized for
	# a human who wants to see the menu) starts recording well after the engine
	# has armed the autoplay at the main menu, and the file opens somewhere in
	# the middle of the caravan chain. Unlike Hatari, nothing here can go back
	# and get the missing minutes. Grab from the start; trim later.
	AMIBERRY_CONF="$conf" DISPLAY="$D" FRUA_AMIGA_DISPLAY="$D" \
	  "$REPO/.claude/skills/run-amiga-port/driver.sh" boot "${AMIGA_BOOT_WAIT:-12}"

	# Grab the amiberry window off X11. x11grab wants a rectangle, so read the
	# window's geometry rather than assuming one — AGA and ECS open different
	# sizes, and a wrong -video_size silently records black margins.
	local geo x y w h
	geo=$(DISPLAY="$D" xwininfo -root -tree | grep -i amiberry | head -1 || true)
	[[ -n "$geo" ]] || { echo "no amiberry window on $D" >&2; return 1; }
	w=$(sed -E 's/.* ([0-9]+)x([0-9]+)\+.*/\1/' <<<"$geo")
	h=$(sed -E 's/.* ([0-9]+)x([0-9]+)\+.*/\2/' <<<"$geo")
	x=$(sed -E 's/.* [0-9]+x[0-9]+\+([0-9-]+)\+([0-9-]+).*/\1/' <<<"$geo")
	y=$(sed -E 's/.* [0-9]+x[0-9]+\+([0-9-]+)\+([0-9-]+).*/\2/' <<<"$geo")
	# x11grab rejects odd dimensions on some encoders; round down to even.
	w=$(( w - w % 2 )); h=$(( h - h % 2 ))
	say "grabbing ${w}x${h}+${x}+${y} on $D"

	# Lossless x264 rather than ffv1: ffv1 is intra-only, and an intra-only
	# stream of a 720x568 screen that barely changes still costs full price per
	# frame — the AGA grab passed 1.2 GB inside three minutes. ultrafast keeps
	# the encoder ahead of a 50 fps realtime grab, which matters here in a way
	# it does not for the offline encode: fall behind and x11grab drops frames.
	ffmpeg -v error -y -f x11grab -framerate 50 -video_size "${w}x${h}" \
	       -i "${D}+${x},${y}" -c:v libx264rgb -qp 0 -preset ultrafast "$RAW" &
	local ffpid=$!

	# The engine's own end marker, out of the Amiga-side DBG.LOG.
	local waited=0
	while ! grep -q 'autoplay: script done' data/work/amiga-mount/DBG.LOG 2>/dev/null; do
		sleep 5; waited=$((waited+5))
		[[ $waited -gt 1200 ]] && { say "TIMED OUT waiting for the script"; break; }
	done
	sleep "${CAPTURE_TAIL:-30}"      # same reason as the Hatari arm above
	kill -INT $ffpid 2>/dev/null || true
	wait $ffpid 2>/dev/null || true
	t1=$(date +%s)
	say "run: $((t1-t0))s wall (grab covers the tail of it)"
	pkill -x amiberry 2>/dev/null || true
}

case "$TARGET" in
falcon) capture_atari falcon ;;
tt)     capture_atari tt ;;
# ONE binary serves Falcon and TT (soft-float -m68020-60), so `tt` rebuilds
# nothing new — the difference is the machine and its TOS, which hatari_ui.sh
# picks from --machine. The ST/STe build is a different binary: CPU68K=68000
# implies -DFRUA_PLANAR, the draw-time bitplane path of ADR-0016.
ste)    capture_atari ste CPU68K=68000 ;;
aga)    capture_amiga "$HOME/Amiberry/Configurations/openua.uae" MACHINE=amiga ;;
ecs)    EXTRA_DEFS=-DFRUA_FORCE_ECS \
        capture_amiga "$HOME/Amiberry/Configurations/openua-ecs.uae" \
                      MACHINE=amiga CPU68K=68000 ;;
*)      echo "unknown target: $TARGET" >&2; exit 1 ;;
esac

say "raw capture -> $RAW ($(du -h "$RAW" | cut -f1))"
