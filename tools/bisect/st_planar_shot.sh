#!/usr/bin/env bash
# bisect_shot.sh <sha> — build <sha> as the SHIPPED ST/STe planar build, drive it
# to the Skull Crag caravan event, and save the frame.
#
# ★ -DFRUA_PLANAR is passed EXPLICITLY and is not optional. It only became the
# CPU68K=68000 default on 2026-07-26 (0be209c9), but release-ste passed it by
# hand before that — so a bare `make CPU68K=68000` silently builds the CHUNKY
# path on the older half of this window and would compare two different
# programs. That mistake already cost one wrong conclusion in this session.
set -euo pipefail

SHA="${1:?usage: bisect_shot.sh <sha>}"
REPO=/home/jfergus/dev/OpenUA
S=/tmp/claude-1000/-home-jfergus-dev-OpenUA/db2f8a2c-03a9-4fef-a53a-58cfb3f7933e/scratchpad
W=$S/bw
G=$REPO/data/work/gamedata-bisect
OUT=$S/bis-$SHA.png

cd "$REPO"
# One reusable worktree; checking out into it keeps whatever object files still
# apply, which matters at ~5 min a rebuild.
if [ ! -d "$W" ]; then
	git worktree add --detach "$W" "$SHA" >/dev/null 2>&1
else
	git -C "$W" checkout --detach -f "$SHA" >/dev/null 2>&1
fi
# data/ is a TRACKED directory in the worktree, so symlink the subdirs INTO it
# (linking data/ itself just nests a link inside the real dir).
for d in work frua-mac dos-frua; do
	[ -e "$REPO/data/$d" ] && ln -sfn "$REPO/data/$d" "$W/data/$d"
done

( cd "$W" && make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_PLANAR' ) > "$S/bis-$SHA.build" 2>&1 || {
	echo "$SHA BUILD-FAILED"; exit 3; }

cp "$W/frua.prg" "$G/frua.prg"
[ -f "$W/frua.rsc" ] && cp "$W/frua.rsc" "$G/frua.rsc"

pgrep -x hatari >/dev/null && { pkill -9 -x hatari; sleep 4; }
rm -f "$G/DBG.LOG" /tmp/frua-ui/conout.log
env -u DISPLAY FRUA_MEM=4 GEMDOS_DIR="$G" \
  HATARI_ARGS="--machine st --tos /usr/share/hatari/tos206us.img --borders no --confirm-quit off" \
  READY_TIMEOUT=300 "$REPO/.claude/skills/run-falcon-port/driver.sh" start > "$S/bis-$SHA.log" 2>&1
# ★ NOT `beginplay`. Its sequence (p a Return Escape b) assumes the Hall's
# letter accelerators ACTIVATE an item. On the older half of this window they
# only HIGHLIGHT it — `a` lights up ADD CHARACTER and nothing opens — so the
# party is never seated, the run ends in the Hall, and the frame classifies as
# neither good nor bad. Observed directly on 0be209c9 by screenshotting each
# key. This sequence sends the extra Return that activates a highlighted item,
# which is harmless on builds where the accelerator already activated.
drive() { env -u DISPLAY GEMDOS_DIR="$G" \
    "$REPO/.claude/skills/run-falcon-port/driver.sh" key "$1" >> "$S/bis-$SHA.log" 2>&1; sleep "$2"; }
drive p 14        # main menu -> Hall
drive a 8         # highlight / open ADD CHARACTER
drive Return 14   # activate it (no-op if already open)
drive Return 14   # commit the highlighted character into the party
drive Escape 10   # leave the add list
drive b 8         # highlight / activate BEGIN ADVENTURING
drive Return 30   # activate it, then let the caravan event paint
# gap=5: this screen LOADS A PICTURE as it opens and the 0.4s default declares
# success mid-load (#106) — that is what produced a black viewport once already.
env -u DISPLAY GEMDOS_DIR="$G" \
  "$REPO/.claude/skills/run-falcon-port/driver.sh" shots "$OUT" 200 40 5 >> "$S/bis-$SHA.log" 2>&1
pgrep -x hatari >/dev/null && pkill -9 -x hatari || true

# Classify by the VIEWPORT ONLY (the HUD is identical either way). Compare
# against the two known references and report both distances; the caller reads
# them rather than trusting a threshold.
convert "$OUT" -crop 250x250+15+10 +repage "$S/vp-$SHA.png"
gp=$(compare -metric AE "$S/vp-good.png" "$S/vp-$SHA.png" null: 2>&1 || true)
bp=$(compare -metric AE "$S/vp-bad.png"  "$S/vp-$SHA.png" null: 2>&1 || true)
echo "$SHA  vs-good=$gp  vs-bad=$bp  -> $OUT"
