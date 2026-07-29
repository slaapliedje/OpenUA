#!/usr/bin/env python3
"""Build BOUNDTEST.DSN — a NON-SQUARE open room, to test the movement bounds check.

Why this exists (task #97). The party's forward step is applied with the two
axis deltas crossed:

    nx = ROW + dir_dx[f]     /* dir_dx is the COLUMN delta */
    ny = COL + dir_dy[f]     /* dir_dy is the ROW    delta */
    if (nx >= 0 && nx < w && ny >= 0 && ny < h) ...   /* each vs the OTHER limit */

Two rounds of testing failed to pin it because both used a SQUARE 10x10 area:

  * round 1 put the party at (5,5), where row == col, so a (row,col) transpose
    maps the cell to itself and cannot be seen;
  * round 2 moved to (3,7) and ruled the display transpose out, but with
    width == height the BOUNDS CHECK is still invisible — both limits are 10,
    so a crossed pair behaves exactly like a correct one.

A non-square room separates them. With w=8 and h=14 a party standing at
ROW 9 has row > width, so a bounds test that compares a ROW-derived value
against the WIDTH fails — and `party_step` returns with no message, which is
exactly the silent no-move round 2 hit and could not explain.

Two entry points, differing ONLY in the row:

    entry 0 — row  9, col 3   row >= w(8): FAILS a swapped bounds check
    entry 1 — row  5, col 3   row <  w(8): passes either way  (the CONTROL)

The room is otherwise completely open (perimeter only), so nothing legitimately
blocks and any refusal is the bounds test. Select the entry with the design's
"AT ENTRY POINT" setting, or drive both by regenerating with --entry.

    python3 tools/mk_boundstest_design.py data/work/gamedata --current
    python3 tools/mk_boundstest_design.py data/work/gamedata --current --entry 1

READ THE HUD's row,col after each key — never the key count.
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
    print("  row %d %s width %d -> a swapped bounds check %s"
          % (row, ">=" if row >= W else "<", W,
             "REFUSES silently" if row >= W else "passes (control)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
