# GEO area file format (`GEOnnn.DAT`)

One `GEOnnn.DAT` inside a `.DSN` design folder is a single **adventure area** —
a level map: its dimensions, wall layout, per-cell event hooks, the area's event
table, and its string table. The runtime loads it in `jt198 → l7226`
(`src/engine/boot.c`) into the design-state buffer (A5 global `-12300`).
`tools/geo.py` reads/writes/builds these files and round-trips them byte-for-byte.

Everything multi-byte is **big-endian** (authored on a 68k Mac). The file is
**always exactly 12962 bytes** — a fixed-layout container, not a variable IFF.

## Container

```
FORM <0x329a = filesize-8>
  AMOD <0x3292 = filesize-16>          ← a SIZED sub-chunk, not a bare IFF
    HDR  <0x122  = 290>   → design-state[0..289]     header
    MAP  <0xd80  = 3456>  → design-state[290..]      cells
    ENCR <0x7d0  = 2000>  → event table  (A5 -13038) 100 × 20 bytes
    STRG <0x1c00 = 7168>  → string table (A5 -13034)
```

`AMOD` carries a size word and doubles as the FORM's type — the "no formType"
FRUA quirk that `l7470`'s FORM branch special-cases (`boot.c:1185`). Each chunk
is `tag(4) + size(4) + body`; sizes here are all even, so no odd-byte padding
occurs in practice (the parser still honours it). `l7226` validates:

- first four bytes ∈ {`FORM`,`CAT `,`LIST`} (else the buffer is treated as raw
  legacy data and accepted as-is);
- `AMOD` size == 12946; each chunk present at its exact fixed size;
- HDR version word ∈ **100..106**;
- `width × height` ∈ **1..576**.

## HDR — design-state[0..289]

| offset | type | field |
|---|---|---|
| 0 | u16 BE | **version** (100..106; real data is 106) |
| 2 | u8 | **width** (columns) |
| 3 | u8 | **height** (rows per column) |
| 14 + i·4 | u8×3 | **entry-point `i`**: `[+0]`=Y, `[+1]`=X, `[+2]`=facing (low 3 bits) — see below |
| 48 + z·4 | u8×4 | **zone `z` rule** (z=0..7): `[+0]`=interrupt event, `[+1]` bit7 = "no resting" |
| 272 + k·2 | u16 | 8 shorts stored **byte-swapped** on disk, flipped via `jt1180` on load |

The remaining HDR bytes hold per-area design metadata (light level, save flags,
etc.) not yet individually mapped; for a bare walkable area they can stay zero.

**Party start.** `l0bbc` places the party from entry point `g_a5_-18488` (the
Game-Settings "AT ENTRY POINT" value): `st = ds + entry*4`, then
`X = st[15]`, `Y = st[14]`, `facing = st[16] & 7`. So entry 0's record lives at
`ds[14..16]`, entry 1 at `ds[18..20]`, and so on (4-byte stride).

## MAP — design-state[290..]

`width × height` cells, **6 bytes each**, laid out **column-major**:
`cell_index = height · col + row` (`jt201`/`jt212`), `col` ∈ [0,width),
`row` ∈ [0,height). Capacity is 576 cells (the MAP chunk is a fixed 3456 bytes;
cells past `width×height` are unused padding).

| byte | meaning |
|---|---|
| 0..3 | the four **edge walls** (direction = `edge/2`, i.e. `edge` 0/2/4/6). High nibble = wall id (0..15, `jt212`); low nibble = wall attribute (door/secret, read `& 15`). |
| 4 | **special** = event index + 1 into ENCR (0 = no event on this cell); `jt201` returns it |
| 5 | **zone** in bits 2..4 (`(byte>>2) & 7`, `jt197`) + misc flags in the other bits |

## ENCR — the event table (A5 `-13038`)

**100 events × 20 bytes.** A cell's `special` byte value *N* selects event
`N-1`. The event dispatcher is `l709e` (`boot.c`), a `JT[3]` switch on byte 0.

### Common header (every event type)

| byte | field |
|---|---|
| 0 | **type** (0..38 — see the table below) |
| 1 | **flags**: bit0 = *once-only* (fires once, then dead); bits 3..7 = **condition type** |
| 2 | **condition parameter** — meaning depends on the condition type |
| 3 | **auto-chain**: index of the next event to run (0 = none) — the default sequencing |
| 7 | **branch-control flags** (bits tested by the branching types, e.g. 36/38) |
| 8,9 | **branch targets / gate** — e.g. type 36 runs event `ev[8]` on yes, `ev[9]` on no |

**Condition types** (`ev[1] >> 3`, checked in `l694e`; the parameter is `ev[2]`):

| ct | fires when |
|---|---|
| 0 | always |
| 1 | design flag `rec[param+69]` is set |
| 2 | design flag `rec[param+69]` is clear |
| 3 / 4 | party is NOT / IS in class band 6..20 |
| 5 | a percent roll ≤ `param` (random chance) |
| 6 / 7 | `rec[25]` ≠ 0 / == 0 |
| 8 | party facing ∈ `param` bitmask (bit 0=N, 1=E, 2=S, 3=W) |

Condition types 9..16 also occur in real data and exist in `l694e` but are not
yet mapped here.

### Event types

Named from `l709e`'s handlers (`tools/geo.py` `EVENT_TYPES`):

| type | name | | type | name |
|---|---|---|---|---|
| 0 | (empty / chain-only) | | 18–20 | Question outcome / branch |
| 1 | **Combat** | | 21 | Encounter |
| 2 | **Message / Text** (most common) | | 22 | Menu meta-event |
| 3, 25 | Give-Take treasure | | 23 | (chain-only) |
| 4 | Affect-party effect | | 24 | Vault |
| 5, 11, 34 | Stairs / passage / level change | | 26 | Award experience |
| 6 | Training Hall | | 27 | Pass time |
| 7 | Tavern | | 29 | Inn |
| 8 | Shop / merchant | | 32 | Select member by class |
| 9 | Give treasure / Temple | | 33 | Combat (fixed) |
| 10 | Encounter (prompt + outcome) | | 35 | Conditional-variable branch |
| 12 | Scripted movement | | 36 | Yes/No Question |
| 13 | HP percentage | | 37 | Set standard rumors |
| 14 | Message (conditional) | | 38 | Set quest-flag |
| 15 | Conditional event | | 16, 17 | **Set variable**, Play sounds |

### Combat event (types 1 & 33) — fully mapped

The combat handler is `l159a` → `l10a0` (monster spawn) → `l0d2a_c20`. **Type 1**
runs *all* the specified monster groups (a fixed battle); **type 33** picks *one*
group at random. Layout (verified against 1590 real combat events, 0 mismatches):

| byte | field |
|---|---|
| 0 | type (1 or 33) |
| 1–3 | common header (condition, once-only, auto-chain) |
| 4–5 | **descriptive text id** — *little-endian* word into the area STRG table (0 = none) |
| 6 | **picture id** — 0 = none, `<240` = sprite/PIC marker, `≥240` = bigpic backdrop |
| 7 | bit7 = picture is a sprite; bits0–6 → combat config (`rec[27]`) |
| 8–19 | **six monster-group slots**, slot *s* at `(ev[8+2s], ev[9+2s])` |

Each monster-group slot:

- `ev[8+2s] & 0x1f` = **count** (1..31; 0 = empty slot)
- `ev[9+2s]` = **monster id** (1..255 → the design's MONST library; 0 = empty slot)
- the **high 3 bits** of each even byte carry combat config flags, e.g. `ev[8]`
  bit5 = "continue after victory", `ev[8]` bits6–7 = surprise (`rec[46]`), `ev[14]`
  bits5–6 = starting range (`rec[56]`), `ev[18]` bits6–7 = picture base (0/2/10/41).

```python
from geo import Geo
g = Geo.blank(8, 8)
g.set_combat(idx=0, groups=[(66, 4), (25, 1)])   # 4× monster 66, 1× monster 25
info = g.combat(0)     # {'groups': [(66,4),(25,1)], 'text_id':…, 'picture':…, 'random':False}
```

### Message / Text event (types 2 & 14) — mapped

The narrative event (`l4d26`). Displays up to five lines of text, each a string
id into the area STRG table, with an optional picture and sound.

| byte | field |
|---|---|
| 4 | per-line **confirm mask** — bit *i* pauses for a click after line *i*; bit5 sets a follow-up flag |
| 6 | picture id (same encoding as Combat) |
| 7 | per-line **style mask** — bit *(i+2)* selects text style 3 vs 7 for line *i* |
| 8,10,12,14,16 | five **text-id word slots** (*little-endian*; 0 = no line) into STRG |
| 18 | event **sound** id |

```python
g.strg_write(["", "You enter a dark cavern.", "A cold wind blows."])
g.set_message(idx=0, text_ids=[2, 3])   # ids are 1-based -> STRG slots 1 and 2
```

### Passage / transfer event (types 5, 11, 34) — mapped

The area-linking event (`l5676`). **Type 11** is a level change: stepping its
cell loads a different area and moves the party there.

| byte | field |
|---|---|
| 4 | confirm/prompt text id (shown when `ev[7]` bit5 asks a yes/no) |
| 7 | bit5 = prompt before transfer; bit6 = invert the answer; bits2–3 = landing **facing** (`(ev[7]&0x0c)>>1` → 0=N 2=E 4=S 6=W) |
| 8 | direct landing **col** (y) |
| 9 | direct landing **row** (x) |
| 12 | bit0 = use a target-area entry **marker** (`ev[13]`) instead of the direct `ev[8]`/`ev[9]` |
| 13 | target entry-marker index (when `ev[12]` bit0 set) |
| 14 | **target area number** — the engine loads `GEO<ev[14]>.DAT` (type 11) |

Types 5/34 move within the current area; only type 11 changes level. A stepped
passage cell fires the transfer — note the engine does **not** fire it on the
party's *initial* placement (only on a move onto the cell), so a passage can't
sit on the very start tile.

```python
g.set_passage(idx=2, dest_area=6, x=3, y=3, facing=0)   # step here -> area 6
g.set_cell(4, 3, walls=(...), special=3)                # cell -> event 2 (special = idx+1)
```

Verified against 947 real passages (`dest_area` decode, 0 mismatches). Linking
areas in `tools/dsn.py`: `d.add_area(5, a5); d.add_area(6, a6)` and a `set_passage`
in area 5 pointing at 6.

### Treasure event (types 3 & 25) — mapped

The loot event (`l28b0`). **Type 3** gives the treasure and refreshes the view;
**type 25** gives only. Money stages into the party pool, items via `jt187`.

| byte | field |
|---|---|
| 4–7 | **platinum** — little-endian u32, bit31 cleared |
| 8–9 | **gems** — little-endian u16 |
| 10–11 | **jewelry** — little-endian u16 |
| 12–19 | up to eight **item id** slots (1..255; 0 = empty) |

```python
g.set_treasure(idx=3, platinum=100, gems=6, jewelry=2, items=[85])   # 100pp + a ring
g.treasure(3)   # {'platinum': 100, 'gems': 6, 'jewelry': 2, 'items': [85], ...}
```

Verified against 748 real treasure events (0 mismatches) + round-trip. Item ids
index the design's item library; gems/jewelry later convert to XP when appraised.

### Temple event (type 9) — mapped

A temple offering built-in cure/resurrect services (`l216a`). The event only
frames it — picture + two text lines:

| byte | field |
|---|---|
| 6 | picture id (defaults to the temple backdrop when 0) |
| 7 | bit3 = add the extra service menu row |
| 13–14 | **intro text id** (little-endian STRG id, shown on entry) |
| 15–16 | **wish/prompt text id** ("what is your wish?") |

```python
g.set_temple(idx, intro_text=13, wish_text=14, healing=True)
```

### Shop event (type 8) — mapped

A merchant (`l5586`): a category and a stock list of items for sale.

| byte | field |
|---|---|
| 5 | **shop type** (merchant category → `rec[40]`) |
| 6 | picture id (defaults to the shop backdrop when 0) |
| 8–19 | four 3-byte **stock slots** — each a `jt188` trigger cell selecting items from the item-selector grid (`docs/item-selector.md`): `kind = slot[0]>>4`, bits pick columns |

`set_shop(shop_type, stock=[...])` packs a list of item-selector indices
(grouped by `index//20`, one row per slot, up to 4). The indices resolve to item
ids through the built-in `-12645` grid — see
[item-selector](item-selector.md) and `tools/itemsel.py`.

### Set-design-variable event (type 16) — mapped

`l6020` — pure arithmetic on the design-variable byte array (`rec[var+69]`,
`rec` = A5 `-28006`). Variables persist in the design state and gate later
events (condition types 1/2 test them).

| byte | field |
|---|---|
| 4 | **op bits**: 0–1 = set(1)/add-saturating(2)/subtract(3) `var[ev5]` by `ev6`; bit2 = AND-reduce; bit3 = OR-reduce; bit4 = reload the play screen |
| 5 | target variable id (set/add/sub) |
| 6 | operand value |
| 7–12 | six source variable ids (AND/OR reduce) |
| 13 | destination variable id — gets `1` if all/any source vars are nonzero |

`set_variable(op=…, target=…, value=…)` or
`set_variable(reduce="and"/"or", sources=[…6], dest=…)`.

### Yes/No Question event (type 36) — mapped

`l3118` — show a question, run a Yes/No modal, and branch. Text ids are STRG
indices (little-endian, like every event text id).

| byte | field |
|---|---|
| 4–5 | **question** text id (LE) |
| 6 | picture id (0 → redraw the 3D view) |
| 7 | branch side-effect flags (0x04/0x08 force-jump on yes/no; 0x20/0x10 set a flag) |
| 8 | **yes-chain** event index (run on YES) |
| 9 | **no-chain** event index (run on NO) |
| 10–11 | yes-response text id (LE) |
| 12–13 | no-response text id (LE) |

`set_question(question, yes_chain=…, no_chain=…, yes_text=…, no_text=…)`.

### Conditional-variable branch (type 35) — mapped (decode)

`l6436` — ask a prompt (six variants via `ev[7]` bits 3–5), record the answer
into variable `ev[8]` (255/increment on yes, 128 on no), and chain to `ev[10]`
(yes) / `ev[11]` (no). `var_branch()` decodes it; the shared branch outcome
framework `l3cd6` (also used by attribute/pay/keyword checks, types 18–20)
encodes yes/no actions in `ev[10]`.

### Other per-type parameters

Combat, Message, Passage, Treasure, Temple, Shop, Variable and Question are
mapped to the byte. The remaining types read their own bytes — a continued
effort, best done per type as a module needs it.

A bare area with no events zero-fills this chunk.

## STRG — the string table (A5 `-13034`)

The area's event text — a **6-bit packed, uppercase-only string pool** of up to
400 strings. Decoded by `l4fbe`/`l4c88`, initialised by `l4db4`. Layout:

| offset | size | contents |
|---|---|---|
| 0 | 6 | header (3 words, **little-endian** — the engine byte-swaps them in `l4e3a`): `[0]` body capacity (6762), `[2]` = 0xffff, `[4]` = 0 |
| 6 | 400 | **length index** — `lt[i]` = packed byte count of string *i* (0 = empty, 255 = unused slot) |
| 406 | 6762 | **body** — the packed character data |

String *i* starts at `body[Σ lt[0..i-1]]` (`l4a30`, skipping 255s); its character
count is `lt[i] * 4 // 3`. The body packs **four 6-bit codes into every three
bytes**; a code maps to a character as: `0` -> pad, `1..31` -> `code + 64` (A-Z,
`[\]^_`), `32..63` -> literal ASCII (space, punctuation, digits). There is **no
lowercase** — text folds to upper case. An event's *text id* is **1-based**
(`jt232` reads string `num-1`), so the displayed line is `strg_read()[text_id-1]`.

**If the whole chunk is NUL the loader re-seeds it** (`l7226` -> `l4db4`), so a
bare area may leave STRG all zero and let the engine initialise an empty table.

```python
g.strg_write(["", "You enter a dark cavern.", "A cold wind blows."])  # slots 0,1,2
g.strg_read()[1]      # 'YOU ENTER A DARK CAVERN.'
```

## Wiring an area into a playable design

A `.DSN` design folder becomes playable with just two files (`tools/dsn.py`):

- **`GAME001.DAT`** — the 388-byte settings record. Byte **48** = the start area
  number, byte **49** = the 1-based start entry; the engine reads these at Play
  (`boot.c` ~19218) and loads `GEO<byte48>.DAT`. Other fields (LE): `[0:32]`
  title, `[32]` XP, `[36]` platinum, `[40]` gems, `[44]` jewelry, `[50]` equip.
- **`GEO<NNN>.DAT`** — one area per file; the start area's number must match byte
  48, and the file must exist (else `jt198` → `jt69` fatal). Levels ≤ 4 render as
  **overland** (wilderness map), ≥ 5 as a **dungeon** (first-person view).

```python
from dsn import Design
from geo import Geo
g = Geo.blank(8, 8)
g.set_entry_point(0, x=3, y=3, facing=0)
# ... set walls / events / strings ...
d = Design("MYMOD", title="My Module")
d.start_area = 5              # a dungeon
d.add_area(5, g)
d.write("path/to/gamedata", make_current=True)   # writes MYMOD.DSN + start.dat
```

Verified end-to-end in Hatari: a generated dungeon loads via Play → Training Hall
→ Begin Adventuring, and the first-person view renders the generated walls with
the party and event text.

## Generating an area

`tools/geo.py`:

```python
from geo import Geo
g = Geo.blank(width=8, height=8)      # zeroed HDR/MAP/ENCR/STRG, version 106
g.set_entry_point(0, x=1, y=1, facing=0)
g.set_cell(col=1, row=1, walls=(0x10, 0, 0, 0))   # a wall on edge 0
open("GEO001.DAT", "wb").write(g.build())          # exactly 12962 bytes
```

`Geo.parse(g.build()) == g` round-trips exactly; verified byte-for-byte against
real tutorial and HEIRS areas (11×24, 15×38, 28×20). A minimal playable area
needs at least a valid entry point on a walkable cell; walls, events and strings
are optional. Placing the area in a design and making it the "begins in" area
(Game Settings) is what wires it into a module — see [create-new-design](../CLAUDE.md).
```
python3 tools/geo.py path/to/GEO001.DAT   # dump a summary of an existing area
```

## The in-engine editor

`geo.py` is the offline, headless-testable way to build an area. The engine
also ships an **interactive map editor** (`jt243`) that edits the same GEO cell
table and saves the same container — see [geo-editor](geo-editor.md) for its
tool palette and data model. It is lifted and **live** (reachable from the main
menu via the `E` key, verified in Hatari); `geo.py` stays the fully
headless-testable path because the editor's cell placement is mouse-driven.
