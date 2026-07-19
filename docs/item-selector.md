# Item-selector grid (`-12645`)

Treasure squares, shop stock, and the editor's item picker do **not** name item
ids directly. They reference a built-in **16 × 20 selector grid** that maps a
`(kind, slot)` pair to an item id (1..255, indexing [ITEM.DAT](item-table.md)).
`tools/itemsel.py` reads it and decodes the trigger cells.

## The grid (A5 `-12645`)

The grid is a flat `16 rows × 20 columns = 320` bytes at A5 `-12645`. It is
**DATA-seeded** — part of the engine's initialised A5 world — and **nothing
writes it at runtime**, so it is a fixed engine taxonomy, identical for every
design. Row layout in the base seed:

| rows | contents |
|---|---|
| 0–1 | mundane weapons (ids 1..31) |
| 2 | armor / shields |
| 3–7 | +1 … +5 magic weapons |
| 8–9 | magic armor |
| 10 | misc + assorted magic |
| 11–15 | rings, wands, potions, scrolls |

Because the values are engine data (and copyrighted), `itemsel.py` extracts them
from the user's own `UnlimitedAdventures.rfork` / `frua.rsc` — never hardcoded,
never committed. The addressing and row/col shape below are structure.

## Trigger cells (`jt188`)

A treasure square or shop stock slot is a **3-byte cell**. `jt188` decodes it:

```
kind  = cell[0] >> 4                          // which of the 16 rows
bit i = cell[2 - (i >> 3)] & (1 << (i & 7))   // for i in 0..19 — which slots
```

so the 20 selection bits pack as:

| byte | bits | slots |
|---|---|---|
| `cell[2]` | 0..7 | slots 0..7 |
| `cell[1]` | 0..7 | slots 8..15 |
| `cell[0]` | 0..3 | slots 16..19 |
| `cell[0]` | 4..7 | **kind** (row) |

Every set bit `i` grants `grid[kind][i]` (built as an item via `jt187`). The
editor's picker (`l348e`) uses the same grid: pick a kind, then a slot, and the
chosen item is `grid[kind][slot]`.

This is what the **shop** stock slots (`ev[8..19]`, four 3-byte cells) and
**treasure**-square triggers resolve through — see [geo-format](geo-format.md).

## Reading

```python
from itemsel import SelectorGrid, make_cell, cell_kind, cell_slots

g = SelectorGrid.from_rfork("UnlimitedAdventures.rfork")
g.item(3, 0)                 # the item id at kind 3, slot 0  (a +1 weapon)
g.items_for_cell(cell)       # item ids a 3-byte shop/treasure cell grants
g.locate(239)                # (kind, slot) of item 239  -> (13, 1)
g.cell_for_item(239)         # a 3-byte cell that grants exactly item 239

make_cell(3, [0, 5, 19])     # build a cell: kind 3, slots 0/5/19
cell_kind(cell), cell_slots(cell)   # decode one
```

Verified: decoding a real design's shop event (GEO001 stock cells
`0f7f5e`/`1ffdef`/`30bdbf`/`4ec73f`) resolves through the grid to the expected
item names — mundane weapons, then +1 and +2 weapons.
