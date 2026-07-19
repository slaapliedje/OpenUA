#!/usr/bin/env python3
"""Read / write the FRUA design item table (`ITEM.DAT`).

A design's items live in **ITEM.DAT** (the "item.dat" the engine loads via L5304
into the A5 item-template table at `-27920`): a flat array of **254 records, 18
bytes each** (item ids 1..254; id 255 is the money pseudo-item, id 0 unused).
Treasure events (`ev[12..19]`) and shop stock select items by these ids, and the
`-12645` treasure/shop selector table maps `(row, bit)` -> an item id.

`jt187` builds a live 62-byte item node from a template: `node[40..57]` is an
18-byte copy of `-27920 + id*18`, with the two words at template `[4..5]`/`[6..7]`
byte-swapped (stored little-endian on disk). `node[40]` (template `[0]`) is the
item TYPE.

Field map (what's confidently identified; the rest is per-type and not fully
mapped — see docs/item-table.md):

    [0]    item type (1..105) — indexes the engine's built-in item name/behaviour
    [3]    secondary type (mirrors [0] in the base table)
    [4..5] little-endian value word (cost/level; per-type meaning)
    [6..7] little-endian value word
    [11]   flags (low 3 bits cleared for a "plain"/give-flagged item)
    [12]   weapon range code ((v-1)//3 = reach in the -27944 table)
    [13]   usable-by class mask

This is host-side tooling; no engine or copyrighted data is needed to build a
fresh item table. `data/` is git-ignored — never commit one.
"""
import struct

ITEM_SIZE   = 18
ITEM_COUNT  = 254                       # ids 1..254 (id 0 unused, 255 = money)
ITEMDAT_SIZE = ITEM_COUNT * ITEM_SIZE   # 4572


class ItemError(ValueError):
    pass


class Item:
    """One 18-byte item template. `.raw` is the mutable bytearray; the named
    properties decode the confidently-identified fields."""

    def __init__(self, raw=None):
        self.raw = bytearray(raw if raw is not None else ITEM_SIZE)
        if len(self.raw) != ITEM_SIZE:
            raise ItemError("item record must be %d bytes" % ITEM_SIZE)

    @property
    def type(self):        return self.raw[0]
    @type.setter
    def type(self, v):     self.raw[0] = v & 0xff; self.raw[3] = v & 0xff

    @property
    def value(self):       return struct.unpack_from("<H", self.raw, 4)[0]
    @value.setter
    def value(self, v):    struct.pack_into("<H", self.raw, 4, v & 0xffff)

    @property
    def value2(self):      return struct.unpack_from("<H", self.raw, 6)[0]
    @value2.setter
    def value2(self, v):   struct.pack_into("<H", self.raw, 6, v & 0xffff)

    @property
    def range_code(self):  return self.raw[12]
    @range_code.setter
    def range_code(self, v): self.raw[12] = v & 0xff

    @property
    def class_mask(self):  return self.raw[13]
    @class_mask.setter
    def class_mask(self, v): self.raw[13] = v & 0xff

    @property
    def empty(self):       return self.raw[0] in (0, 255)

    def __repr__(self):
        return ("Item(type=%d value=%d value2=%d range=%d class_mask=0x%02x)"
                % (self.type, self.value, self.value2,
                   self.range_code, self.class_mask))


class ItemTable:
    """A design's ITEM.DAT — 254 item templates (1-based ids)."""

    def __init__(self, items=None):
        self.items = list(items) if items is not None else \
            [Item() for _ in range(ITEM_COUNT)]
        if len(self.items) != ITEM_COUNT:
            raise ItemError("table must hold %d items" % ITEM_COUNT)

    def __getitem__(self, item_id):
        """Item by 1-based id (as treasure/shop reference it)."""
        if not (1 <= item_id <= ITEM_COUNT):
            raise IndexError("item id %d out of 1..%d" % (item_id, ITEM_COUNT))
        return self.items[item_id - 1]

    @classmethod
    def parse(cls, data):
        if len(data) != ITEMDAT_SIZE:
            raise ItemError("ITEM.DAT must be %d bytes, got %d"
                            % (ITEMDAT_SIZE, len(data)))
        return cls([Item(data[i * ITEM_SIZE:(i + 1) * ITEM_SIZE])
                    for i in range(ITEM_COUNT)])

    def build(self):
        out = bytearray()
        for it in self.items:
            out += it.raw
        assert len(out) == ITEMDAT_SIZE
        return bytes(out)

    def used_ids(self):
        """Ids (1-based) whose record is non-empty."""
        return [i + 1 for i, it in enumerate(self.items) if not it.empty]


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    t = ItemTable.parse(open(argv[0], "rb").read())
    used = t.used_ids()
    print("ITEM.DAT %s: %d/%d items used" % (argv[0], len(used), ITEM_COUNT))
    for iid in used[:16]:
        print("  id %3d: %r" % (iid, t[iid]))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
