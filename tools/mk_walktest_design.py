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
    python3 tools/mk_walktest_design.py data/work/gamedata --current --pillars

Then build with `-DFRUA_AUTOPLAY -DFRUA_AUTOWALK` and soak. READ THE POSITION
READOUT, not the key count: the HUD prints `col,row`, so a screenshot proves
whether the party moved. That is the check the earlier runs lacked.

`--pillars` adds four free-standing blocks in the corners of the interior. The
bare room is USELESS for spotting render artefacts, which was not obvious until
the first STE walk was captured (task #61): a square chamber viewed from its
centre looks the SAME from all four facings, and a plain wall four cells away
looks much the same three cells away, so a six-step walk produced a viewport
that never changed a pixel — only the clock moved. Nothing can be seen to glitch
in a picture that never differs. The pillars sit off the walk path, so they
break that symmetry without blocking a step: each facing frames a different
block, and stepping changes its size. Use `--pillars` whenever the point is to
LOOK at the viewport; the bare room is still right for pure input/timing runs.

Room is 10x10 with the party at the centre, which leaves at least four clear
cells in every direction — the autowalk script's six steps interleaved with
four turns cannot leave the room, so no step is silently swallowed by a wall
bump. (A bump is harmless — the engine just says there is no way to go — but it
would make a step invisible in the position readout and muddy the result.)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, _walled_room                      # noqa: E402

ROOM = 10
ENTRY_ROW, ENTRY_COL = 5, 5


# Free-standing blocks FLANKING the walk axes, one cell to either side of the
# party's column/row. Each is a cell walled on all four sides, so the renderer
# draws it as a solid pillar.
#
# They flank rather than sit in the interior corners because the first attempt
# put them at (2,2)/(7,2)/(2,7)/(7,7) and the viewport STILL never changed: the
# first-person view is a narrow forward cone, and a block three cells off-axis
# is simply not in it. Flanking blocks enter the cone as the party approaches
# and grow as it passes between them, while leaving the centre column and row
# clear so no step turns into a wall bump.
PILLARS = ((4, 3), (6, 3), (4, 7), (6, 7))


def walktest_design(name="WALKTEST", pillars=False):
    a5 = _walled_room(w=ROOM, h=ROOM, entry=(ENTRY_ROW, ENTRY_COL), facing=0)
    if pillars:
        for col, row in PILLARS:
            a5.set_cell(col, row, walls=(0x10, 0x10, 0x10, 0x10))
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
    pillars = "--pillars" in argv
    d = walktest_design(pillars=pillars)
    folder = d.write(argv[0], make_current=("--current" in argv))
    print("wrote", folder)
    print("  %dx%d room, party at (col %d, row %d), NO events on any cell"
          % (ROOM, ROOM, ENTRY_COL, ENTRY_ROW))
    print("  pillars: %s" % ("4 free-standing blocks (viewport varies per step)"
                             if pillars else "none (viewport is STATIC — see the docstring)"))
    print("  verify the walk by the HUD's col,row readout — never by the key count")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
