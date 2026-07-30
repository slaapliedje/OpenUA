"""Pin tools/geo.py's map-axis pairing against the ENGINE's invariant (#104).

WHY THIS TEST EXISTS
--------------------
v0.5.12-beta shipped an inverted forward step (#98) because four separate
"measurements" of which map coordinate is which were all wrong together. They
were wrong together because they were CIRCULAR: `tools/geo.py` decides which GEO
axis it calls the row, a generated fixture inherits that labelling, and the
in-game HUD it was read back through is labelled from the same assumption. Every
arm of the experiment agreed with the belief under test.

So this file settles the question the two ways that are NOT circular:

  1. AGAINST THE ASM. The engine's invariant is transcribed here, literally,
     from docs/coord-audit.md's evidence table (L5baa / jt210 / jt312 / jt297 /
     L1908 / jt292 — five independent sites). The test then asserts geo.py
     implements exactly that. The expected formula is written out below; it is
     NOT read from geo.py, so geo.py cannot vote on its own correctness.

  2. AGAINST AUTHORED SSI DATA. Real HEIRS areas, whose coordinates were chosen
     by SSI's designers and are read by the Mac engine — no tool labelled them.
     Two of them refute the transposed reading in OPPOSITE directions, so no
     consistent relabelling can survive both. See the docstring on that test.

THE ANSWER, and it is a naming question only
--------------------------------------------
geo.py's ARITHMETIC IS ALREADY THE ENGINE'S — there is no transpose to fix. The
engine computes `(A * ds[3] + B) * 6` with A bounded by ds[2] and B by ds[3];
geo.py computes `(height * col + row) * 6` with col bounded by hdr[2] (= ds[2])
and row by hdr[3] (= ds[3]). So col ≡ A and row ≡ B, exactly.

What differs is vocabulary. geo.py calls A "col" and hdr[2] "width"; the
asm-derived reading calls A the ROW and ds[3] the width (because the two step
tables form a clean 8-point compass ring, which makes -11693 the row delta, and
the Mac adds -11693 to slot A = -12287). Both namings are internally consistent
and the engine cannot tell them apart. Renaming geo.py would flip the meaning of
every existing call site over exactly the arithmetic that must not change — the
same argument coord-audit.md makes for not renaming boot.c. So the names stay and
the PAIRING is pinned here instead.
"""
import glob
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from geo import Geo


# --- the engine's invariant, transcribed from the asm (docs/coord-audit.md) ---
#
#   cell = ds + 290 + (A * ds[3] + B) * 6 + edge
#     A: A5 -12287, takes the -11693 step table, bounded by ds[2], MULTIPLIED
#     B: A5 -12288, takes the -11684 step table, bounded by ds[3], ADDED
#
# In GEO-file terms ds[2] is hdr[2] and ds[3] is hdr[3]. geo.py's parameter
# names for (A, B) are (col, row); that correspondence is what this file pins.
CELL_SIZE = 6


def _engine_cell_offset(hdr2, hdr3, a, b):
    """The engine's own index arithmetic, written out independently of geo.py."""
    del hdr2                            # bounds only; not part of the index
    return (a * hdr3 + b) * CELL_SIZE


# --- 1. geo.py implements the asm invariant (always runs, no data needed) -----

def test_cell_index_matches_engine_formula():
    """set_cell must land its bytes exactly where the engine would look.

    Deliberately NON-SQUARE and with A != B, so a transposed implementation
    cannot coincide: on a 5x7 map, A=3/B=6 lands at a different offset than
    A=6/B=3 would, and the latter is not even representable.
    """
    g = Geo.blank(5, 7)                 # geo.py: width=5, height=7
    assert g.width == 5 and g.height == 7
    assert (g.hdr[2], g.hdr[3]) == (5, 7), "width/height must be hdr[2]/hdr[3]"

    a, b = 3, 6                         # A=col=3 (<hdr[2]), B=row=6 (<hdr[3])
    marker = (0x21, 0x43, 0x65, 0x87)
    g.set_cell(a, b, walls=marker, special=0x5A, zone=3)

    off = _engine_cell_offset(g.hdr[2], g.hdr[3], a, b)
    assert tuple(g.map[off:off + 4]) == marker, (
        "set_cell(col=%d, row=%d) did not land at the engine's offset %d "
        "— the axis pairing has been transposed" % (a, b, off))
    assert g.map[off + 4] == 0x5A
    # and reading it back agrees
    assert g.cell(a, b)[:4] == bytes(marker)
    assert g.cell_special(a, b) == 0x5A
    assert g.cell_zone(a, b) == 3


def test_cell_bounds_are_the_engine_pairing():
    """A is bounded by hdr[2] and B by hdr[3] — not the other way round.

    This is the half a SQUARE map can never check, and the half #97/#98 got
    wrong. On a 5x7 map the two bounds differ, so each direction is testable.
    """
    g = Geo.blank(5, 7)

    g.set_cell(4, 6)                            # the far corner is legal
    with pytest.raises(IndexError):
        g.set_cell(5, 0)                        # A == hdr[2] -> out
    with pytest.raises(IndexError):
        g.set_cell(0, 7)                        # B == hdr[3] -> out
    # And the transposed limits must NOT be the live ones: A may reach 4 (>=
    # nothing special) while B may legally reach 6, which a hdr[2]-bounded B
    # would forbid.
    g.set_cell(0, 6)                            # legal only if B < hdr[3]=7
    with pytest.raises(IndexError):
        g.set_cell(6, 0)                        # illegal only if A < hdr[2]=5


def test_entry_point_byte_order():
    """l0bbc reads st[15] as the coordinate `cell` calls row and st[14] as col
    (docs/geo-format.md, 'Party start'). Pin both the store and the readback."""
    g = Geo.blank(5, 7)
    g.set_entry_point(0, row=6, col=3, facing=2)
    assert g.hdr[14] == 3, "hdr[base+14] must be col (the MULTIPLIED axis, A)"
    assert g.hdr[15] == 6, "hdr[base+15] must be row (the ADDED axis, B)"
    assert g.entry_point(0) == (6, 3, 2), "entry_point returns (row, col, facing)"

    g.set_entry_point(1, row=1, col=2, facing=5)     # 4-byte stride
    assert (g.hdr[18], g.hdr[19]) == (2, 1)
    assert g.entry_point(1) == (1, 2, 5)
    assert g.entry_point(0) == (6, 3, 2), "entry 1 must not disturb entry 0"


# --- 2. authored SSI data refutes the transpose (opt-in: needs the game data) --

def _real_geo_files():
    root = os.path.join(os.path.dirname(__file__), "..",
                        "data", "work", "gamedata", "HEIRS.DSN")
    return sorted(glob.glob(os.path.join(root, "GEO0*.DAT")))


REAL = _real_geo_files()
ENTRIES = 4                             # 4-byte stride from hdr[14]; hdr is 290 B


def _entries_of(g):
    out = []
    for i in range(ENTRIES):
        row, col, facing = g.entry_point(i)
        if (row, col) != (0, 0):        # (0,0) is the unused/degenerate record
            out.append((i, row, col, facing))
    return out


@pytest.mark.skipif(not REAL, reason="no data/work/gamedata (copyrighted GEO absent)")
def test_authored_entry_points_are_in_bounds_under_geo_py_labelling():
    """Every SSI-authored entry point must satisfy col < hdr[2], row < hdr[3].

    Non-circular: these coordinates were chosen by the module's human designers
    and are consumed by the Mac engine. No tool of ours labelled them.
    """
    checked = 0
    for path in REAL:
        g = Geo.parse(open(path, "rb").read())
        for i, row, col, _f in _entries_of(g):
            assert 0 <= col < g.width, (
                "%s entry %d: col %d outside hdr[2]=%d"
                % (os.path.basename(path), i, col, g.width))
            assert 0 <= row < g.height, (
                "%s entry %d: row %d outside hdr[3]=%d"
                % (os.path.basename(path), i, row, g.height))
            checked += 1
    assert checked >= 10, "expected a useful number of authored entries, got %d" % checked


@pytest.mark.skipif(not REAL, reason="no data/work/gamedata (copyrighted GEO absent)")
def test_authored_data_refutes_the_transposed_labelling_both_ways():
    """THE DISCRIMINATOR. Find authored entries that the TRANSPOSED reading
    (col bounded by hdr[3], row bounded by hdr[2]) puts out of range — in BOTH
    directions, so no consistent relabelling can survive.

    Measured on HEIRS 2026-07-30:
      GEO008/009/010/018/019  hdr[2]=28 hdr[3]=20, entry col=27
                              -> 27 < 28 ✔ but 27 < 20 ✗   (col IS hdr[2]-bound)
      GEO011                  hdr[2]=21 hdr[3]=24, entry row=23
                              -> 23 < 24 ✔ but 23 < 21 ✗   (row IS hdr[3]-bound)

    One violation alone would only prove the labelling is not uniformly swapped;
    two in opposite directions pins each axis to its own bound.
    """
    col_needs_hdr2 = []                 # col >= hdr[3]: col cannot be hdr[3]-bound
    row_needs_hdr3 = []                 # row >= hdr[2]: row cannot be hdr[2]-bound

    for path in REAL:
        g = Geo.parse(open(path, "rb").read())
        name = os.path.basename(path)
        for i, row, col, _f in _entries_of(g):
            if col >= g.height:
                col_needs_hdr2.append((name, i, col, g.width, g.height))
            if row >= g.width:
                row_needs_hdr3.append((name, i, row, g.width, g.height))

    assert col_needs_hdr2, (
        "no authored area proves col is bounded by hdr[2] — the transposed "
        "reading survives, so this suite is not actually discriminating")
    assert row_needs_hdr3, (
        "no authored area proves row is bounded by hdr[3] — the transposed "
        "reading survives, so this suite is not actually discriminating")


@pytest.mark.skipif(not REAL, reason="no data/work/gamedata (copyrighted GEO absent)")
def test_authored_areas_fit_the_map_chunk_under_this_labelling():
    """width*height must fit the fixed 576-cell MAP chunk for every real area —
    a cheap whole-file cross-check that hdr[2]/hdr[3] are being read as the two
    map extents at all (and not, say, off by a field)."""
    for path in REAL:
        g = Geo.parse(open(path, "rb").read())
        assert 0 < g.width and 0 < g.height
        assert g.width * g.height <= 576, (
            "%s: %dx%d = %d cells exceeds the 576-cell MAP chunk"
            % (os.path.basename(path), g.width, g.height, g.width * g.height))
