#!/usr/bin/env bash
# icongrid_repro.sh — pass/fail probe for the char-gen body-icon grid going
# COLOURLESS (#137 regression: the sprites render as black/silhouette instead of
# in colour). Built to be git-bisect'able: exits 0 when the grid is COLOURED
# (good), 1 when it is not (bad), 125 when the build fails (skip this commit).
#
# The bug only appears through the LIVE route, not the FRUA_BODY harness, and
# only on a 4MB machine's data set — so this drives the real menu path
# (p Return c d d <name> Return) on --memsize 4 against a copy of the shipping
# card data. The accelerators matter: D commits those buttons, clicks do not.
#
# Verdict is taken from the grid region's colour content, not a screenshot diff:
# count DISTINCT palette colours among the cell pixels. A coloured grid shows
# many; a black one collapses to a handful of greys.
set -u

# Repo root. Derived from the script's own location by default, but OVERRIDABLE:
# git bisect runs the probe from a copy outside the tree (the tracked copy
# vanishes on checkouts of commits that predate it), and there "$0/.." resolves
# to / — which has no Makefile, so every iteration reported a bogus "build
# failed / skip". Losing a whole bisect to that is why this is a variable.
REPO=${ICONGRID_REPO:-$(cd "$(dirname "$0")/.." && pwd)}
GAME=${ICONGRID_GAME:-/tmp/claude-1000/-home-jfergus-dev-OpenUA/db2f8a2c-03a9-4fef-a53a-58cfb3f7933e/cardgame}
SHOT=${ICONGRID_SHOT:-/tmp/icongrid_verdict.png}
D="$REPO/.claude/skills/run-falcon-port/driver.sh"

cd "$REPO" || exit 125
pkill -9 -x hatari 2>/dev/null; sleep 1
rm -f "$GAME/DBG.LOG"

# Build, RETRYING ONCE. The first make after a commit transition also runs the
# BUILDSTAMP purge (objects from the other commit are deleted and everything
# rebuilds), and that one-shot transition is where this failed during bisect —
# every manual re-run afterwards succeeded, which is exactly what made it look
# like an unbuildable commit. A second attempt on an already-purged tree is a
# genuine build test; only a repeat failure means "skip this commit".
if ! make CPU68K=68000 >/tmp/icongrid_make.log 2>&1; then
	echo "icongrid: first make failed (purge transition?), retrying" >&2
	if ! make CPU68K=68000 >/tmp/icongrid_make.log 2>&1; then
		echo "icongrid: BUILD FAILED twice -> skip commit" >&2
		tail -5 /tmp/icongrid_make.log >&2
		exit 125
	fi
fi
cp frua.prg "$GAME/FRUA.PRG"                     || exit 125

env -u DISPLAY HATARI_ARGS="--machine megaste --memsize 4" \
    GEMDOS_DIR="$GAME" timeout 250 "$D" start >/dev/null 2>&1 || { pkill -9 -x hatari; exit 125; }

# The live route, one key per call. PACING IS LOAD-BEARING: driver.sh start
# returns on "menu: modal up", but the menu is not ready for input for another
# couple of seconds, and each screen needs time to compose before the next
# accelerator lands. Driving this by hand only worked because the screenshots
# between steps supplied the delay; without it every key is swallowed and the
# run sits on the main menu reporting a false PASS.
sleep 4
for k in p Return c d d T E S T Return; do
	env -u DISPLAY "$D" key "$k" >/dev/null 2>&1
	sleep 2
done
sleep 3
env -u DISPLAY "$D" shots "$SHOT" >/dev/null 2>&1
pkill -9 -x hatari 2>/dev/null

[ -s "$SHOT" ] || exit 125

# Grid occupies roughly the left panel. Count distinct colours there.
n=$(convert "$SHOT" -crop 300x340+110+70 +repage -format %c histogram:info:- 2>/dev/null \
    | wc -l)
echo "icongrid: distinct colours in grid region = $n"
# Coloured grid measures far above this; a black/silhouette grid far below.
[ "${n:-0}" -ge 12 ] && exit 0 || exit 1
