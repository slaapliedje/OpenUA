# Item table (`ITEM.DAT`)

A design's items live in **`ITEM.DAT`** — the "item.dat" the engine loads (via
`L5304` → the GLIB loader) into the A5 item-template table at `-27920`. It is a
flat array of **254 records, 18 bytes each** (item ids **1..254**; id 0 is
unused, id 255 is the money pseudo-item). `tools/items.py` reads and builds it.

## How item ids are referenced

- **Treasure events** name up to eight item ids in `ev[12..19]` (see
  [geo-format](geo-format.md)).
- **Shop** stock and treasure-cell triggers select ids through the **`-12645`
  selector table** (`jt188`: `id = (slot[0]>>4)*20 + bit`).
- `jt187(id, flag)` builds a live 62-byte item node: `node[40..57]` is an 18-byte
  copy of `-27920 + id*18`, with the words at template `[4..5]`/`[6..7]`
  byte-swapped (they are little-endian on disk).

## Record layout (18 bytes)

| offset | field | confidence |
|---|---|---|
| 0 | **item type** (1..105) — category/behaviour; for base items equals the primary name-word index `[3]` | confirmed (`node[40]`) |
| 1 | **name-word index** — trailing modifier word (emitted last) | confirmed (`jt28`; see [item-names](item-names.md)) |
| 2 | **name-word index** — modifier word (emitted second) | confirmed (`jt28`) |
| 3 | **name-word index** — primary noun (emitted first); usually == `type` | confirmed (`jt28`) |
| 4–5 | little-endian **value** word (cost / level; per-type) | partial |
| 6–7 | little-endian value word | partial |
| 11 | flags (the give-flag clears its low 3 bits) | confirmed (`jt187`) |
| 12 | **weapon range** code (`(v-1)//3` = reach in the `-27944` table) | confirmed |
| 13 | usable-by **class mask** | confirmed (`ITEMS.DAT class mask, entry byte 13`) |

The remaining bytes and the exact per-type meaning of `[4..7]` depend on the
item type (a weapon, ring, potion and scroll read different fields) and are
**not yet fully mapped** — `tools/items.py` exposes the confirmed fields as named
properties and the whole record as `.raw` for the rest.

## Not yet mapped

- The per-type field semantics of `[4..7]` (damage, AC bonus, charges, spell id).
- The **`-12645` selector table** source (built into the design state; not traced
  to a standalone file yet).
- **ITEMS.DAT** (a separate 2048-byte design file) vs `ITEM.DAT` — this doc
  covers `ITEM.DAT`, the 254×18 template table `jt187` reads.

## Reading / building

```python
from items import ItemTable
t = ItemTable.parse(open("ITEM.DAT", "rb").read())
t[85]              # Item(type=4 value=10 value2=300 range=0 class_mask=0x00)  (a ring)
t[10].type = 5; t[10].class_mask = 0xff
open("ITEM.DAT", "wb").write(t.build())     # 4572 bytes
```

`ItemTable.parse(t.build()) == t` round-trips byte-for-byte (verified against the
base `ITEM.DAT`, all 254 records).
