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
- `jt187(id, flag)` builds a live 62-byte item node: `node[40..57]` is a verbatim
  18-byte copy of `-27920 + id*18`. The two little-endian words at `[4..5]`
  (weight) / `[6..7]` (value) are byte-swapped to native by the save/load path
  (`jt578` via `jt1180`), not by `jt187`. See [item-fields](item-fields.md).

## Record layout (18 bytes)

| offset | field | confidence |
|---|---|---|
| 0 | **item type** (1..105) — category/behaviour; for base items equals the primary name-word index `[3]` | confirmed (`node[40]`) |
| 1 | **name-word index** — trailing modifier word (emitted last) | confirmed (`jt28`; see [item-names](item-names.md)) |
| 2 | **name-word index** — modifier word (emitted second) | confirmed (`jt28`) |
| 3 | **name-word index** — primary noun (emitted first); usually == `type` | confirmed (`jt28`) |
| 4–5 | **weight** — little-endian; tenths of a pound (Plate 450 = 45 lb) | confirmed (`l0b3e`, `boot.c:91244`) |
| 6–7 | **value** — little-endian; appraised worth in gp (`jt932` pricing) | confirmed (`boot.c:92627`) |
| 8 | **to-hit / damage bonus**, signed (`+N`; 253 = −3 cursed) | confirmed (`l0be0`, `jt28`) |
| 9 | **AC / save bonus**, signed | confirmed (`l0be0`) |
| 10 | usability / identify flag | confirmed (`jt28`, `l2d78`) |
| 11 | known-name-parts mask (give-flag clears its low 3 bits) | confirmed (`jt187`, `jt28`) |
| 13 | **stack / ammo count** (Arrow 20, Dart 10) | confirmed (`jt28`, `l32c4`) |
| 14–16 | **charges / spell-id / hook** — per-type (wand: charges+spell; scroll: 3 spell ids; worn: `[16]` bit7 = passive) | confirmed (`l2d78`) |

Full per-type detail and confidence levels are in
[item-fields](item-fields.md). Bytes `[12]` and `[17]` are unused in the base
table. **Weapon range and usable-by class are NOT in this template** — they live
in the per-type behaviour table at A5 `-27944` (16-byte stride, keyed by item
type), read by `l0be0` / `l0b3e`.

## Not yet mapped

- The **`-12645` selector table** source (built into the design state; not traced
  to a standalone file yet).
- **ITEMS.DAT** (a separate 2048-byte design file) vs `ITEM.DAT` — this doc
  covers `ITEM.DAT`, the 254×18 template table `jt187` reads.

## Reading / building

```python
from items import ItemTable
t = ItemTable.parse(open("ITEM.DAT", "rb").read())
t[85]              # Item(type=4 name_word=4 weight=5 value=300 hit=+0 ac=+0 count=0)
t[10].type = 5; t[10].hit_bonus = 2; t[10].value = 500
open("ITEM.DAT", "wb").write(t.build())     # 4572 bytes
```

`ItemTable.parse(t.build()) == t` round-trips byte-for-byte (verified against the
base `ITEM.DAT`, all 254 records).
