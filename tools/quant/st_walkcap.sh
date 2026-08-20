#!/usr/bin/env bash
# Capture the quantiser's inputs across a LONG dungeon walk (#139 "pin the palette").
#
# ★ WHY THIS EXISTS. The FRUA_QDUMP capture in st_reband fires once per re-band,
# and the earlier 14-capture set held only TWO dungeon walks, near-identical —
# so "one fixed palette across many walk screens" could not be priced at all.
# The fix is not a new capture mode, it is a LONGER DRIVE: during the walk the
# re-band trigger is the new-ink overflow path (st_patch_new_ink declining past
# INK_MAX), so every re-band a walk causes is, by construction, a frame whose
# colours the current palette does NOT cover — exactly the frames a pinned
# palette would have to survive. Walk far enough and the per-re-band dump IS
# the dataset.
#
# The drive is st_profile.sh's, minus the profiler: boot -> beginplay -> the
# HEIRS entry-event chain -> arrows until the capture budget is spent. Read
# that script's comments before changing the key sequence; every step in it is
# load-bearing (the six Returns, the treasure screen's e/n, the ten farewells).
#
# Build first:
#   make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_QDUMP -DFRUA_PALDIAG'
# Then:
#   tools/quant/st_walkcap.sh [max_keys] [outdir]
#
# ★ THE CAPTURES ARE FRAME BUFFERS OF COPYRIGHTED ART. They land in the
# git-ignored gamedata mount and are copied to $OUT (default /tmp). NEVER
# commit one — see #139's note on dd228b1d.
set -u
MAXKEYS="${1:-260}"
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${2:-/tmp/frua-walkcap}"
GD="$REPO/data/work/gamedata"
HATARI="${HATARI_BIN:-$HOME/opt/hatari/bin/hatari}"
TOS="${ST_TOS:-/usr/share/hatari/tos206us.img}"
# The drive itself is machine-agnostic — only the emulated hardware differs — so
# the same script can ask a CHUNKY backend the same question as a planar one.
# That is how a "the ST planar path did it" theory gets falsified cheaply:
#   ST_MACHINE=falcon ST_TOS=/usr/share/hatari/TOSv4.04.img ... st_walkcap.sh
MACHINE="${ST_MACHINE:-ste}"
XD="${FRUA_XVFB_DISPLAY:-:99}"

[ -x "$REPO/frua.prg" ] || { echo "no frua.prg — build it first"; exit 1; }
mkdir -p "$OUT"; rm -f "$OUT"/q*.frm "$OUT"/q*.clt "$OUT"/DBG.LOG

# ★ CLEAR THE PREVIOUS RUN'S CAPTURES FROM THE MOUNT. qd_n restarts at 0 every
# boot and Fcreate truncates, so a shorter run leaves the TAIL of a longer one
# behind and the analysis silently mixes two drives.
rm -f "$GD"/q[0-9][0-9].frm "$GD"/q[0-9][0-9].clt
: > "$GD/DBG.LOG" 2>/dev/null || true

pkill -9 -x hatari 2>/dev/null; sleep 1
if ! DISPLAY="$XD" xdpyinfo >/dev/null 2>&1; then
	echo "starting Xvfb on $XD"
	setsid Xvfb "$XD" -screen 0 1280x1024x24 </dev/null >/dev/null 2>&1 &
	sleep 2
fi

( DISPLAY="$XD" SDL_VIDEODRIVER=x11 "$HATARI" \
    --machine "$MACHINE" --memsize 4 --tos "$TOS" --fast-forward yes --conout 2 \
    -d "$GD" --auto 'C:\FRUA.PRG' \
    < /dev/null > "$OUT/run.log" 2>&1 & )
trap 'pkill -9 -x hatari 2>/dev/null' EXIT

find_wid() { DISPLAY="$XD" xwininfo -root -tree 2>/dev/null \
	| grep -i hatari | head -1 | awk '{print $1}'; }
send() { DISPLAY="$XD" xdotool key --window "$1" "$2" 2>/dev/null; }
ready() { grep -q 'menu: modal up' "$OUT/run.log" 2>/dev/null \
       || grep -q 'menu: modal up' "$GD/DBG.LOG" 2>/dev/null; }

echo "waiting for the menu ..."
for _ in $(seq 1 72); do
	ready && break
	pgrep -x hatari >/dev/null || { echo "emulator exited during boot — void"; exit 3; }
	sleep 5
done
ready || { echo "never saw 'menu: modal up' — void"; exit 3; }

WID=$(find_wid); [ -n "$WID" ] || { echo "no Hatari window on $XD"; exit 1; }
D="${PLAY_STEP_DELAY:-3}"
echo "menu up — entering the dungeon"
for step in p a Return Escape b; do
	send "$WID" "$step"
	[ "$step" = b ] && sleep $((D * 2)) || sleep "$D"
done
sleep "$D"

echo "clearing the HEIRS entry-event chain"
for _ in 1 2 3 4 5 6; do send "$WID" Return; sleep "$D"; done
send "$WID" e; sleep "$D"        # EXIT the treasure screen (it ignores Return)
send "$WID" n; sleep "$D"        # NO, do not go back for the rest
for _ in $(seq 1 10); do send "$WID" Return; sleep 1; done
sleep "$D"

# ★ STOP ON THE CAPTURE BUDGET, NOT THE KEY COUNT. The dump numbers q00..q99 and
# WRAPS, so a drive that outlives 100 re-bands overwrites its own early frames
# and the set silently stops being a time series. Watch the mount and stop at 99.
echo "walking (max $MAXKEYS keys, stop at 99 captures)"
KEYS=(Up Left Up Right Up Down Up Left Return Up Right Up Left Up Right Return)
i=0
while [ "$i" -lt "$MAXKEYS" ]; do
	pgrep -x hatari >/dev/null || { echo "emulator exited after $i keys"; break; }
	n=$(ls "$GD"/q[0-9][0-9].frm 2>/dev/null | wc -l)
	[ "$n" -ge 99 ] && { echo "capture budget full at $i keys"; break; }
	send "$WID" "${KEYS[$((i % ${#KEYS[@]}))]}"
	i=$((i + 1))
	sleep 0.7
done
echo "drove $i keys"

# ★ FRUA_SHOT: grab the live frame after the walk, for a correctness A/B that
# the MSE numbers cannot give (a cached palette that is subtly WRONG scores
# well and looks broken). Three grabs two seconds apart, because a single one
# catches a half-drawn frame — the walk clears the viewport and redraws it, and
# 40% of this drive's re-bands fire on the cleared half.
if [ -n "${FRUA_SHOT:-}" ]; then
	for k in 1 2 3; do
		sleep 2
		DISPLAY="$XD" import -window "$WID" "$OUT/shot$k.png" 2>/dev/null || true
	done
fi

sleep 3
pkill -9 -x hatari 2>/dev/null; sleep 1
cp -f "$GD"/q[0-9][0-9].frm "$GD"/q[0-9][0-9].clt "$OUT/" 2>/dev/null
cp -f "$GD/DBG.LOG" "$OUT/DBG.LOG" 2>/dev/null
echo
echo "captures: $(ls "$OUT"/q*.frm 2>/dev/null | wc -l) frames in $OUT"
grep -E '^inkdiag:' "$OUT/DBG.LOG" 2>/dev/null | tail -20
