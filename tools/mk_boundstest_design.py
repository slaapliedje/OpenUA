#!/usr/bin/env python3
"""Build BOUNDTEST.DSN — a NON-SQUARE open room (8 wide x 14 high, perimeter only).

★★ DO NOT USE THIS FIXTURE TO SETTLE AN AXIS QUESTION. It cannot, and #97/#98
believed it could — which shipped an inverted forward step in v0.5.12-beta.

Why it cannot: THIS SCRIPT decides which GEO axis it calls the "row" (via
`set_entry_point(idx, row, col, facing)` and `set_cell(c, r, ...)`), and the
in-game HUD you read the result back from is labelled from the same assumption.
So the fixture agrees with whatever you already believed. Four separate
measurements on this file and WALKTEST were mutually consistent and all wrong
together. Breaking the geometric symmetries — non-square map, asymmetric cell,
five-step trajectory — was necessary and *still* not sufficient, because the
labelling was never in the experiment.

  * An axis/pairing question is settled from the Mac asm (the invariant is
    tabulated in docs/coord-audit.md section 3) and confirmed on an
    SSI-AUTHORED module, where events sit on squares a human author chose:
    HEIRS entry 10,8 -> one step -> 11,8 announces "THE WEARY WANDERER".
    Arriving at the right authored content is evidence no labelling can fake.

What this fixture IS still good for: a completely open non-square room, i.e. a
place where nothing legitimately blocks, so any refusal to move is the BOUNDS
check rather than a wall. That is a real use — just not a directional one.

Two entry points, differing only in the first coordinate:

    entry 0 — 9, 3
    entry 1 — 5, 3

★ AND THE PREMISE OF THAT PAIR IS WRONG — kept visible rather than deleted,
because it is the exact mistake (#104 re-checked it 2026-07-30). The first
coordinate `set_entry_point` takes is the `row`, and `row` is bounded by
`height` = 14, NOT by `W` = 8. So entry 0's "9" is comfortably IN range and
this fixture never tested a bounds refusal at all; the original comment
("9 exceeds the 8-wide axis") silently assumed the labelling it was built to
prove. See the correspondence table above `Geo.width` in tools/geo.py.

    python3 tools/mk_boundstest_design.py data/work/gamedata --current
    python3 tools/mk_boundstest_design.py data/work/gamedata --current --entry 1

Read the HUD pair after each key, never the key count — and use PLAY_NUDGE=0,
since beginplay's Right+Left nudge is net-zero only if both keys land.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dsn import Design, Geo, ART_BLOCK, WALL_SOLID          # noqa: E402

W, H = 8, 14                       # deliberately NON-SQUARE
ENTRIES = [(9, 3), (5, 3)]         # (row, col): row 9 >= W, row 5 < W


def boundstest_geo(entry_idx=0):
    g = Geo.blank(W, H)
    g.hdr[4:14] = bytes(ART_BLOCK)          # the area's art binding (#94)

    # Perimeter only. A cell is drawn from ITS OWN edge bytes, so the boundary
    # ring is what stops the party leaving; the interior is entirely open.
    for r in range(H):
        for c in range(W):
            n = WALL_SOLID if r == 0     else 0
            e = WALL_SOLID if c == W - 1 else 0
            s = WALL_SOLID if r == H - 1 else 0
            w = WALL_SOLID if c == 0     else 0
            g.set_cell(c, r, walls=(n, e, s, w))

    row, col = ENTRIES[entry_idx]
    g.set_entry_point(0, row, col, 0)       # facing 0 = compass N
    return g, row, col


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    dest = argv[0]
    entry_idx = 0
    if "--entry" in argv:
        entry_idx = int(argv[argv.index("--entry") + 1])

    g, row, col = boundstest_geo(entry_idx)
    d = Design("BOUNDTEST")
    d.start_area = 5           # level >= 5 -> dungeon (first-person) mode
    d.add_area(5, g)
    d.write(dest)
    if "--current" in argv:
        d.set_current(dest)

    print("wrote %s/BOUNDTEST.DSN" % dest)
    print("  %dx%d (NON-SQUARE: w=%d h=%d), open interior, perimeter walls" % (W, H, W, H))
    print("  entry %d: row %d, col %d, facing 0 (compass N)" % (entry_idx, row, col))
    # NB: this line prints the ORIGINAL (mistaken) framing — row is bounded by
    # H, not W, so it says nothing about a real bounds check. See the docstring.
    print("  row %d %s W %d -- MEANINGLESS as a bounds test (row is H-bound)"
          % (row, ">=" if row >= W else "<", W))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
