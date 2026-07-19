#!/usr/bin/env python3
"""The FRUA item-selector grid (the A5 `-12645` table).

Treasure cells and shop stock don't name item ids directly — they reference a
built-in **16 x 20 selector grid** that maps a (kind, slot) pair to an item id
(1..255, indexing ITEM.DAT). The grid lives at A5 `-12645`, is **DATA-seeded**
(the engine's fixed item taxonomy — nothing writes it at runtime, so it is the
same for every design), and is read by:

  * `jt188(cell)` — a 3-byte trigger cell (treasure squares, shop stock slots):
      kind  = cell[0] >> 4                       (which of the 16 rows)
      bit i = cell[2 - (i >> 3)] & (1 << (i & 7))  for i in 0..19  (which slots)
    every set bit i grants item grid[kind][i] via jt187. So the 20 selection
    bits pack as: cell[2] = slots 0..7, cell[1] = slots 8..15,
    cell[0] low nibble = slots 16..19.

  * `l348e` — the editor's item picker: choose a kind (row), then a slot; the
    result is grid[kind][slot].

Row taxonomy in the base grid (from the DATA seed):
    0  basic weapons (ids 1..20)          8-9   magic armor
    1  more weapons                       10    misc + special
    2  armor / shields                    11-15 rings / wands / potions / scrolls
    3-7  +1..+5 magic weapons

The grid values are engine data (extracted from the user's own resource fork,
like the item name words — never hardcoded, never committed). `data/` is
git-ignored. The bit-addressing and row/col shape are structure, implemented
here directly.
"""
import struct
import sys

SELECTOR_A5_OFFSET = -12645     # base of the grid in the A5 world
SELECTOR_ROWS      = 16
SELECTOR_COLS      = 20
SELECTOR_SIZE      = SELECTOR_ROWS * SELECTOR_COLS   # 320 bytes


class SelectorError(ValueError):
    pass


def extract_grid(rfork_path):
    """Extract the 16x20 selector grid from a FRUA resource fork.

    The grid is part of the DATA-seeded A5 world; replay DATA+ZERO and read the
    320 bytes at A5 -12645. Returns a list of 16 rows, each a list of 20 ints.
    """
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from macrsrc import ResourceFork
    import datapool as dp

    rf = ResourceFork.from_file(rfork_path)
    try:
        image = dp.expand_data(rf.get("DATA", 0).data, rf.get("ZERO", 0).data)
    except KeyError as e:
        raise SelectorError("%s missing DATA/ZERO (%s)" % (rfork_path, e))
    base = len(image) + SELECTOR_A5_OFFSET
    flat = image[base:base + SELECTOR_SIZE]
    if len(flat) != SELECTOR_SIZE:
        raise SelectorError("selector grid runs past the DATA image")
    return [list(flat[r * SELECTOR_COLS:(r + 1) * SELECTOR_COLS])
            for r in range(SELECTOR_ROWS)]


def cell_kind(cell):
    """The grid row a 3-byte trigger cell selects (its high nibble)."""
    return (cell[0] >> 4) & 15


def cell_slots(cell):
    """The list of slot indices (0..19) whose bit is set in a 3-byte cell —
    matches jt188's `cell[2 - (i>>3)] & (1 << (i&7))` addressing."""
    out = []
    for i in range(20):
        if cell[2 - (i >> 3)] & (1 << (i & 7)):
            out.append(i)
    return out


def make_cell(kind, slots):
    """Build the 3-byte trigger cell for a kind (row) and iterable of slot
    indices (0..19). Inverse of cell_kind/cell_slots."""
    if not (0 <= kind < SELECTOR_ROWS):
        raise SelectorError("kind %d out of 0..15" % kind)
    cell = bytearray(3)
    cell[0] = (kind & 15) << 4
    for i in slots:
        if not (0 <= i < SELECTOR_COLS):
            raise SelectorError("slot %d out of 0..19" % i)
        cell[2 - (i >> 3)] |= 1 << (i & 7)
    return bytes(cell)


class SelectorGrid:
    """The 16x20 item-selector grid. Build from a resource fork with
    `from_rfork`, or pass a list-of-rows directly (tests use a synthetic one)."""

    def __init__(self, rows):
        self.rows = [list(r) for r in rows]
        if len(self.rows) != SELECTOR_ROWS or \
           any(len(r) != SELECTOR_COLS for r in self.rows):
            raise SelectorError("grid must be %dx%d"
                                % (SELECTOR_ROWS, SELECTOR_COLS))

    @classmethod
    def from_rfork(cls, rfork_path):
        return cls(extract_grid(rfork_path))

    def item(self, kind, slot):
        """The item id at (kind, slot)."""
        return self.rows[kind][slot]

    def items_for_cell(self, cell):
        """The list of item ids a 3-byte trigger cell grants (jt188 semantics):
        the kind's row, indexed by every set selection bit."""
        row = self.rows[cell_kind(cell)]
        return [row[i] for i in cell_slots(cell) if row[i]]

    def locate(self, item_id):
        """Find (kind, slot) for an item id, or None. Useful to author a cell
        that grants a specific item."""
        for r, row in enumerate(self.rows):
            for c, v in enumerate(row):
                if v == item_id:
                    return (r, c)
        return None

    def cell_for_item(self, item_id):
        """A 3-byte trigger cell that grants exactly `item_id`, or None if the
        item is not in the grid."""
        loc = self.locate(item_id)
        return make_cell(loc[0], [loc[1]]) if loc else None


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    g = SelectorGrid.from_rfork(argv[0])
    for r in range(SELECTOR_ROWS):
        row = g.rows[r]
        used = [v for v in row if v]
        if used:
            print("row %2d: %s" % (r, " ".join("%3d" % v for v in row)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
