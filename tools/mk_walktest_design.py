#!/usr/bin/env python3
"""Build WALKTEST.DSN — an EVENT-FREE room, so a scripted walk actually walks.

Why this exists. `FRUA_AUTOWALK` drives the party forward to exercise the one
render path a full present does not cover: `present_rect`, the small viewport
update a step produces. Pointed at HEIRS it never worked, and the failure was
invisible — HEIRS opens a MODAL event chain on its entry cell (the Skull Crag
caravan messages, then the treasure screen, then the XP award panel), and
movement keys sent into a modal are discarded. The run logs a perfect 24/24 keys
with the party standing still, so the key count says "walk sampled" while
nothing moved. Two soaks were spent before the last frame gave it away.

This module removes the cause rather than fighting it: one walled chamber, party
in the middle, **no events anywhere** — no `set_event`, no `_hook`, so every
cell's `special` stays 0 and nothing can open a modal. The walk keys reach the
walk code.

    python3 tools/mk_walktest_design.py data/work/gamedata --current
    python3 tools/mk_walktest_design.py data/work/gamedata --current --corridor

Then build with `-DFRUA_AUTOPLAY -DFRUA_AUTOWALK` and soak. READ THE POSITION
READOUT, not the key count: the HUD prints `row,col` (measured 2026-07-29 --
an earlier line here said col,row and was wrong), so a screenshot proves
whether the party moved. That is the check the earlier runs lacked.

`--corridor` gives the cells around the entry SIDE walls. The
bare room is USELESS for spotting render artefacts, which was not obvious until
the first STE walk was captured (task #61): a square chamber viewed from its
centre looks the SAME from all four facings, and a plain wall four cells away
looks much the same three cells away, so a six-step walk produced a viewport
that never changed a pixel — only the clock moved. Nothing can be seen to glitch
in a picture that never differs. The side walls break that symmetry without
blocking a step, since they are perpendicular to travel: each facing frames a
wall as it passes. Use `--corridor` whenever the point is to LOOK at the
viewport; the bare room is still right for pure input/timing runs.

Room is 10x10 with the party at the centre, which leaves at least four clear
cells in every direction — the autowalk script's six steps interleaved with
four turns cannot leave the room, so no step is silently swallowed by a wall
bump. (A bump is harmless — the engine just says there is no way to go — but it
would make a step invisible in the position readout and muddy the result.)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room, WALL_SOLID          # noqa: E402

ROOM = 10
ENTRY_ROW, ENTRY_COL = 5, 5


# Cells given SIDE walls, so a party walking through them is flanked by masonry
# that grows and slides past as it steps. Keyed by the axis the party travels:
# a cell north/south of the entry gets West+East walls, one east/west of it gets
# North+South walls. Those faces are perpendicular to travel, so no step is ever
# blocked.
#
# ★ WALLS ARE PER-CELL EDGES, NOT SHARED BETWEEN NEIGHBOURS. This is the whole
# reason two earlier attempts drew nothing. Both tried free-standing "pillar"
# cells walled on all four sides — first in the interior corners, then flanking
# the walk axes. The pillars were genuinely in the GEO file (verified by reading
# it back with geo.py) and still never appeared, because a cell is drawn from
# ITS OWN wall bytes: the pillar's east wall belongs to the pillar, and the
# corridor cell beside it has nothing on that edge to render. To be seen, a wall
# must sit on an edge of a cell the party stands in or looks through.
_S = WALL_SOLID
SIDE_WALLED = {
    (5, 4): (0x00, _S, 0x00, _S),        # N of entry: walls W and E
    (5, 3): (0x00, _S, 0x00, _S),
    (5, 6): (0x00, _S, 0x00, _S),        # S of entry
    (4, 5): (_S, 0x00, _S, 0x00),        # W of entry: walls N and S
    (6, 5): (_S, 0x00, _S, 0x00),        # E of entry
}


def walktest_design(name="WALKTEST", corridor=False):
    a5 = _walled_room(w=ROOM, h=ROOM, entry=(ENTRY_ROW, ENTRY_COL), facing=0)
    if corridor:
        for (col, row), walls in SIDE_WALLED.items():
            a5.set_cell(col, row, walls=walls)
    # Deliberately NO set_event / _hook: an event-free floor is the whole point.
    d = Design(name, title="Walk-path soak room (no events)")
    d.xp = 15000
    d.start_area = 5                     # >= 5 -> dungeon mode
    d.start_entry = 1
    d.add_area(5, a5)
    return d


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    corridor = "--corridor" in argv
    d = walktest_design(corridor=corridor)
    folder = d.write(argv[0], make_current=("--current" in argv))
    print("wrote", folder)
    print("  %dx%d room, party at (col %d, row %d), NO events on any cell"
          % (ROOM, ROOM, ENTRY_COL, ENTRY_ROW))
    print("  corridor walls: %s"
          % ("%d cells given side walls (viewport varies per step)" % len(SIDE_WALLED)
             if corridor else "none (viewport is STATIC — see the docstring)"))
    print("  verify the walk by the HUD's row,col readout — never by the key count")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
