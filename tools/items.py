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

Field map (code-confirmed against the engine readers; see docs/item-fields.md
for the full per-type detail and confidence levels):

    [0]    item type (category) — indexes the -27944 behaviour table; for base
           items also == the primary name-word index [3]
    [1..3] name-word indices ([3] primary noun) — see docs/item-names.md
    [4..5] WEIGHT   (little-endian; tenths of a pound — Plate 450 = 45 lb)
    [6..7] VALUE    (little-endian; appraised worth in gp; jt932 pricing input)
    [8]    to-hit / damage bonus (signed: +N, or 253=-3 for cursed)
    [9]    AC / save bonus (signed)
    [10]   usability / identify flag
    [11]   known-name-parts mask (which words show before the item is identified)
    [13]   stack / ammo count (Arrow 20, Dart 10)
    [14]   charges (wands / consumables)
    [15]   spell / effect id (dispatched via l77a0 / l2d78)
    [16]   hook: bit7 = worn/passive effect; low 7 bits = hook kind (JT[1]@0x2da2)

Weapon RANGE and usable-by CLASS are NOT in this template — they live in the
per-type behaviour table at A5 -27944 (16-byte stride, keyed by item type),
read by the engine's l0be0 / l0b3e. Fields [12] and [17] are unused in the base
table. For scrolls, [14..16] instead hold up to three spell ids.

This is host-side tooling; no engine or copyrighted data is needed to build a
fresh item table. `data/` is git-ignored — never commit one.
"""
import struct


def _s8(b):
    """Interpret an unsigned byte as a signed 8-bit value."""
    return b - 256 if b >= 128 else b

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
    def type(self, v):
        # [0] is the item category (indexes the -27944 behaviour table); for
        # BASE items it also equals the primary name-word index [3], so mirror
        # it there for convenience. Set `.name_word` afterwards to decouple.
        self.raw[0] = v & 0xff
        self.raw[3] = v & 0xff

    @property
    def name_word(self):   return self.raw[3]        # primary name-word index
    @name_word.setter
    def name_word(self, v): self.raw[3] = v & 0xff

    # --- the two little-endian value words at [4..5] / [6..7] --------------
    @property
    def weight(self):      return struct.unpack_from("<H", self.raw, 4)[0]
    @weight.setter
    def weight(self, v):   struct.pack_into("<H", self.raw, 4, v & 0xffff)

    @property
    def value(self):       return struct.unpack_from("<H", self.raw, 6)[0]
    @value.setter
    def value(self, v):    struct.pack_into("<H", self.raw, 6, v & 0xffff)

    # --- per-item static fields (see docs/item-fields.md) -----------------
    @property
    def hit_bonus(self):   return _s8(self.raw[8])    # to-hit/damage, signed
    @hit_bonus.setter
    def hit_bonus(self, v): self.raw[8] = v & 0xff

    @property
    def ac_bonus(self):    return _s8(self.raw[9])    # AC/save bonus, signed
    @ac_bonus.setter
    def ac_bonus(self, v): self.raw[9] = v & 0xff

    @property
    def usable(self):      return self.raw[10]        # identify/usability flag
    @usable.setter
    def usable(self, v):   self.raw[10] = v & 0xff

    @property
    def known_mask(self):  return self.raw[11]        # name-parts shown pre-ID
    @known_mask.setter
    def known_mask(self, v): self.raw[11] = v & 0xff

    @property
    def count(self):       return self.raw[13]        # stack / ammo count
    @count.setter
    def count(self, v):    self.raw[13] = v & 0xff

    @property
    def charges(self):     return self.raw[14]        # wand/consumable charges
    @charges.setter
    def charges(self, v):  self.raw[14] = v & 0xff

    @property
    def effect_id(self):   return self.raw[15]        # spell/effect id (l77a0)
    @effect_id.setter
    def effect_id(self, v): self.raw[15] = v & 0xff

    @property
    def hook(self):        return self.raw[16]        # bit7=worn; low7=hook kind
    @hook.setter
    def hook(self, v):     self.raw[16] = v & 0xff

    @property
    def worn_effect(self): return bool(self.raw[16] & 0x80)

    @property
    def empty(self):       return self.raw[0] in (0, 255)

    def __repr__(self):
        return ("Item(type=%d name_word=%d weight=%d value=%d hit=%+d ac=%+d "
                "count=%d)" % (self.type, self.name_word, self.weight,
                               self.value, self.hit_bonus, self.ac_bonus,
                               self.count))


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
