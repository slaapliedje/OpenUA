"""Tests for tools/mk_walktest_design.py. All synthetic — no copyrighted data.

There is one property worth pinning, and it is the whole reason the tool exists:
EVERY CELL MUST BE EVENT-FREE. The generator feeds `FRUA_AUTOWALK`, which drives
the party forward to exercise the `present_rect` path a full present never
covers. Pointed at a module whose cells carry events, the walk keys land in a
modal event and are discarded — and the failure is INVISIBLE, because the
autoplay log still reports every key sent. Two full emulator soaks were spent on
exactly that before the last frame gave it away.

So a future edit that adds an event to this room — a message to make the room
less bare, a transfer to make it a corridor — would silently disarm the harness
again rather than break it loudly. This test is that alarm.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from mk_walktest_design import (walktest_design, ROOM, ENTRY_ROW, ENTRY_COL,
                                SIDE_WALLED)


def test_every_cell_is_event_free():
    """The invariant the harness depends on: no cell points at an event."""
    g = walktest_design().areas[5]
    offenders = [(c, r)
                 for c in range(ROOM) for r in range(ROOM)
                 if g.cell_special(c, r) != 0]
    assert offenders == [], (
        "walktest cells must carry NO events — a modal event eats the "
        "autowalk keys and the soak silently samples nothing: %r" % (offenders,))


def test_corridor_is_also_event_free():
    """--corridor must not smuggle an event in: same alarm, other variant."""
    g = walktest_design(corridor=True).areas[5]
    offenders = [(c, r)
                 for c in range(ROOM) for r in range(ROOM)
                 if g.cell_special(c, r) != 0]
    assert offenders == []


def test_corridor_walls_are_perpendicular_to_travel():
    """The side walls exist so the viewport actually CHANGES as the party walks
    (task #61: without them a soak photographs the same frame six times). Two
    earlier attempts used free-standing pillar cells walled on all four sides
    and drew NOTHING, because FRUA walls are PER-CELL EDGES, not shared between
    neighbours — a pillar's east wall belongs to the pillar, and the corridor
    cell beside it has nothing on that edge to draw. So the walls must sit on
    cells the party occupies, and on faces PERPENDICULAR to travel, or they
    would block the step instead of framing it."""
    g = walktest_design(corridor=True).areas[5]
    N, E, S, W = 0, 1, 2, 3
    for (col, row), walls in SIDE_WALLED.items():
        assert g.cell(col, row)[:4] == list(walls) or \
               tuple(g.cell(col, row)[:4]) == walls
        if col == ENTRY_COL:                 # party travels N/S through it
            assert walls[N] == 0 and walls[S] == 0, \
                "(%d,%d) would block a north/south step" % (col, row)
            assert walls[E] and walls[W], "(%d,%d) frames nothing" % (col, row)
        else:                                # party travels E/W through it
            assert walls[E] == 0 and walls[W] == 0, \
                "(%d,%d) would block an east/west step" % (col, row)
            assert walls[N] and walls[S], "(%d,%d) frames nothing" % (col, row)
    # the entry cell itself stays completely open
    assert tuple(walktest_design(corridor=True).areas[5]
                 .cell(ENTRY_COL, ENTRY_ROW)[:4]) == (0, 0, 0, 0)
    # and the bare room must stay bare
    bare = walktest_design().areas[5]
    for (col, row) in SIDE_WALLED:
        assert tuple(bare.cell(col, row)[:4]) == (0, 0, 0, 0)


def test_room_leaves_room_to_walk():
    """The party must have clear cells in every direction, so a step cannot be
    swallowed by a wall bump (harmless in itself, but it makes a step invisible
    in the HUD position readout, which is how the walk is verified)."""
    margin = 4                       # the autowalk script's longest one-way run
    assert ENTRY_COL - margin >= 0 and ENTRY_COL + margin < ROOM
    assert ENTRY_ROW - margin >= 0 and ENTRY_ROW + margin < ROOM


def test_perimeter_is_walled_and_interior_is_open():
    g = walktest_design().areas[5]
    # North edge of row 0 and south edge of the last row are walls...
    assert g.wall(0, 0, 0) != 0
    assert g.wall(0, ROOM - 1, 2) != 0
    # ...and the cell the party stands on is open on every side.
    for d in range(4):
        assert g.wall(ENTRY_COL, ENTRY_ROW, d) == 0


def test_design_is_dungeon_mode_and_loadable():
    d = walktest_design()
    assert d.start_area >= 5          # >= 5 is what puts the engine in a dungeon
    assert 5 in d.areas
    assert len(d.game001()) > 0
