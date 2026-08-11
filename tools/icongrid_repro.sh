#!/usr/bin/env bash
# icongrid_repro.sh — bisectable probe for the char-gen body-icon grid rendering
# COLOURLESS (#137: the 49 sprites came out black/silhouette instead of in
# colour). Exit 0 = coloured (good), 1 = black (bad), 125 = could not judge
# (build failed, or the route never reached the grid) so git bisect SKIPS.
#
# Root-caused and fixed in 12917a32 (l09dc hand-rolled a two-range CLUT install
# where CODE 17 @0x0a5e is one `jsr JT[124]`; the GLIB pool flip on 2026-07-13
# made jt468 return the extracted sub-GLIB, so the hand-rolled extra l37aa step
# walked a level too deep and installed no palette at all). This probe survives
# to date that regression independently and to catch it coming back.
#
# FALCON, not the 68000 ST. The bug reproduces identically on Falcon/VIDEL,
# ST-Low and NovaGPU — three backends sharing no display code, which is what
# proved it was engine-level — and Falcon boots in ~10s against the ST's ~200s.
# It also disproves the 4MB-ceiling theory: Falcon shows it with 14MB.
set -u

# Repo root, OVERRIDABLE: git bisect runs the probe from a copy outside the tree
# (the tracked copy vanishes on checkouts predating it), and there "$0/.."
# resolves to /, which has no Makefile — so every iteration reported a bogus
# "build failed / skip". A whole bisect was lost to that; hence the variable.
REPO=${ICONGRID_REPO:-$(cd "$(dirname "$0")/.." && pwd)}
GAME=${ICONGRID_GAME:-/tmp/claude-1000/-home-jfergus-dev-OpenUA/db2f8a2c-03a9-4fef-a53a-58cfb3f7933e/cardgame}
SHOT=${ICONGRID_SHOT:-/tmp/icongrid_verdict.png}
D="$REPO/.claude/skills/run-falcon-port/driver.sh"

# The 7x7 cell panel, in screenshot pixels (690x602 Falcon window).
GRID=${ICONGRID_GRID:-340x345+35+90}

# VERDICT THRESHOLDS, measured on real frames of all four screens the route can
# land on. The point of two separate measures is that "black grid" and "never
# got to the grid" are DIFFERENT answers — the previous metric (count of
# distinct colours) could not tell them apart, and in fact scored the main menu
# (14) ABOVE a correct grid (12), so it was capable of calling a healthy commit
# bad. Measured:
#
#   screen                meanL   satpx
#   coloured grid (good)  121.2   29.1%
#   black grid (bad)        3.8    0.0%
#   main menu              88.3    2.7%
#   PICK race/class        90.7    6.1%
#
# Both are plain statistics, deliberately: a reference IMAGE would be a crop of
# the game's rendered UI, i.e. copyrighted art, which must not enter the repo.
SAT_GOOD=${ICONGRID_SAT_GOOD:-15}   # satpx >= this  -> coloured grid
DARK_BAD=${ICONGRID_DARK_BAD:-20}   # meanL <= this  -> black grid

# classify <png> -> prints good | bad | other
classify() {
	local f=$1 meanL satpx
	[ -s "$f" ] || { echo other; return; }
	meanL=$(convert "$f" -crop "$GRID" +repage -colorspace Gray \
	        -format "%[fx:int(mean*255)]" info: 2>/dev/null)
	satpx=$(convert "$f" -crop "$GRID" +repage -colorspace HSL \
	        -channel G -separate -threshold 25% \
	        -format "%[fx:int(mean*100)]" info: 2>/dev/null)
	: "${meanL:=999}" "${satpx:=0}"
	if   [ "$satpx" -ge "$SAT_GOOD" ]; then echo "good $meanL $satpx"
	elif [ "$meanL" -le "$DARK_BAD" ]; then echo "bad $meanL $satpx"
	else                                    echo "other $meanL $satpx"
	fi
}

# Calibration mode: classify existing PNGs and exit, no emulator. Use this to
# re-check the thresholds against saved frames whenever the UI changes —
# `icongrid_repro.sh --classify good.png bad.png menu.png` should print
# good / bad / other in that order.
if [ "${1:-}" = "--classify" ]; then
	shift
	for f in "$@"; do printf "%-28s %s\n" "$(basename "$f")" "$(classify "$f")"; done
	exit 0
fi

cd "$REPO" || exit 125
pkill -9 -x hatari 2>/dev/null; sleep 1
rm -f "$GAME/DBG.LOG"

# Build, RETRYING ONCE. The first make after a commit transition also runs the
# BUILDSTAMP purge (objects from the other commit are deleted, everything
# rebuilds), and that one-shot transition is where this failed during an earlier
# bisect — every manual re-run afterwards succeeded, which is exactly what made
# it look like an unbuildable commit. A second attempt on an already-purged tree
# is a genuine build test; only a repeat failure means "skip".
if ! make >/tmp/icongrid_make.log 2>&1; then
	echo "icongrid: first make failed (purge transition?), retrying" >&2
	if ! make >/tmp/icongrid_make.log 2>&1; then
		echo "icongrid: BUILD FAILED twice -> skip commit" >&2
		tail -5 /tmp/icongrid_make.log >&2
		exit 125
	fi
fi
cp frua.prg "$GAME/FRUA.PRG" || exit 125

env -u DISPLAY GEMDOS_DIR="$GAME" timeout 250 "$D" start >/dev/null 2>&1 \
	|| { echo "icongrid: boot failed -> skip" >&2; pkill -9 -x hatari; exit 125; }

# The live route. driver.sh start returns on "menu: modal up", but the menu is
# not ready for input for another moment and each screen needs time to compose.
sleep 4
for k in p Return c d d T E S T Return; do
	env -u DISPLAY "$D" key "$k" >/dev/null 2>&1
	sleep 2
done

# PACING BY POLLING, not by a fixed sleep. Keep screenshotting until the frame
# classifies as a grid (either colour) or we run out of patience. Fixed sleeps
# were tuned on a 16MHz run and silently desynced on anything slower — an older
# commit that booted at 8MHz sat on the PICK screen and got judged there, which
# is how a healthy commit produced a "bad" verdict. `shots` already waits for a
# settled frame, so each poll is itself a stability check.
verdict=other; meanL=; satpx=
for _ in 1 2 3 4 5 6 7 8 9 10 11 12; do
	env -u DISPLAY "$D" shots "$SHOT" >/dev/null 2>&1
	read -r verdict meanL satpx <<<"$(classify "$SHOT")"
	[ "$verdict" != other ] && break
	sleep 4
done
pkill -9 -x hatari 2>/dev/null

echo "icongrid: verdict=$verdict meanL=$meanL satpx=$satpx (thresholds sat>=$SAT_GOOD dark<=$DARK_BAD)"
case "$verdict" in
	good) exit 0 ;;
	bad)  exit 1 ;;
	*)    echo "icongrid: never reached the grid -> SKIP (not a verdict)" >&2
	      exit 125 ;;
esac
