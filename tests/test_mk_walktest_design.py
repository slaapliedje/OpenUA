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
                                PILLARS)


def test_every_cell_is_event_free():
    """The invariant the harness depends on: no cell points at an event."""
    g = walktest_design().areas[5]
    offenders = [(c, r)
                 for c in range(ROOM) for r in range(ROOM)
                 if g.cell_special(c, r) != 0]
    assert offenders == [], (
        "walktest cells must carry NO events — a modal event eats the "
        "autowalk keys and the soak silently samples nothing: %r" % (offenders,))


def test_pillars_are_also_event_free():
    """--pillars must not smuggle an event in: same alarm, other variant."""
    g = walktest_design(pillars=True).areas[5]
    offenders = [(c, r)
                 for c in range(ROOM) for r in range(ROOM)
                 if g.cell_special(c, r) != 0]
    assert offenders == []


def test_pillars_are_solid_and_off_the_walk_path():
    """The pillars exist to break the chamber's symmetry so the viewport
    actually CHANGES as the party walks — without that a soak photographs the
    same frame six times (task #61). They must not sit where the party walks,
    or a step turns into a wall bump and goes missing from the readout."""
    g = walktest_design(pillars=True).areas[5]
    for col, row in PILLARS:
        for d in range(4):
            assert g.wall(col, row, d) != 0, "pillar (%d,%d) not solid" % (col, row)
        # Must not sit ON the walk axes (that would bump a step)...
        assert col != ENTRY_COL and row != ENTRY_ROW, (
            "pillar (%d,%d) sits on the party's walk axis" % (col, row))
        # ...but must FLANK one, or it never enters the forward view cone and
        # the viewport stays static, which is the bug this variant exists to
        # avoid (corner pillars failed exactly that way).
        assert abs(col - ENTRY_COL) == 1 or abs(row - ENTRY_ROW) == 1, (
            "pillar (%d,%d) is too far off-axis to enter the view cone"
            % (col, row))
    # and the bare room must stay bare
    bare = walktest_design().areas[5]
    for col, row in PILLARS:
        assert bare.wall(col, row, 0) == 0


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
