# Game Data & the ECL VM (non-graphics)

A playable engine needs more than images. This is **layer 4** input: the ECL bytecode VM,
GEO maps, MON/ITEM tables, 6-bit packed strings, and saves. **This is future work** — the aim
here is to document the *shape* of each payload and a port path, not to claim a full decode.

Status legend: **DECODED** (verified in repo) · **PARTIAL** (structure known, payload not
fully decoded) · **TO-DECODE** (shape known from hackdocs, not yet exercised on real bytes).

| Payload | Where | Status |
| ------- | ----- | ------ |
| Container walk (DAX/DAA/HLIB) | all data files | **DECODED** (loaders.md) |
| 6-bit packed strings | item/monster names, GEO text, STRG | **TO-DECODE** (algorithm known) |
| ECL bytecode VM | `ECL*.DAX`, `ECL.GLB` | **TO-DECODE** (use COAB reimpl as spec) |
| GEO maps | `GEO*.DAX`, `GEO.GLB` | **PARTIAL** (FRUA layout documented; CoK/DoK close) |
| MON tables | `MON*` / `MONCHA.GLB` | **PARTIAL** (RLE-packed; field layout per-game) |
| ITEM tables | `ITEM*.DAX` / `ITEM.DAT` | **PARTIAL** (18-byte record documented) |
| Save format | `*.sav/.gam/.qch` etc. | **TO-DECODE** (SAVGAM.TXT) |

---

## 1. 6-bit packed strings — TO-DECODE (algorithm verified from docs)

The series packs names/short text **6 bits per character** (`STRGFORM.TXT`, `GEODATA.TXT`):
- 3 bytes hold 4 characters.
- Decode each 6-bit value `v`: `32..63` → ASCII verbatim; `1..31` → `v + 64` (range 65..95,
  i.e. uppercase letters); `0` → end-of-string terminator.
- Caveat: ASCII `@` (64) maps to 0 and so reads as end-of-string — designs avoid it.

```ts
function unpack6bit(bytes: Uint8Array, maxChars = 15): string {
  let bits = 0, acc = 0, out = '';
  for (const b of bytes) {
    acc = (acc << 8) | b; bits += 8;
    while (bits >= 6) {
      bits -= 6; const v = (acc >> bits) & 0x3F;
      if (v === 0) return out;
      out += String.fromCharCode(v >= 32 ? v : v + 64);
      if (out.length >= maxChars) return out;
    }
  }
  return out;
}
```
Feeds `StringTable`, `Item.nameCodes` resolution (via `VOCAB.TXT` vocabulary), `Monster.name`,
GEO zone/text strings.

---

## 2. ECL bytecode VM — TO-DECODE (the big one; reference, don't reinvent)

ECL = the **event/dialogue/combat scripting bytecode**, one program per block in `ECL*.DAX`
(CoK/DoK, three area files) / `ECL.GLB` (DQK, one 446 KB GLB). The container is walkable today
(loaders.md); the **bytecode itself is not yet decoded in this repo**.

> **Important correction for whoever ports this:** `hackdocs_extracted/OPCODES.TXT` is an
> **x86 instruction reference for the CKIT/UA editor executable**, NOT the ECL VM opcode set.
> `SCRIPT.TXT` documents the *editing-form* (`SCRIPT.GLB`) layout, not the runtime bytecode
> either. Do **not** treat either as the ECL VM spec.

**The ECL VM spec of record is the Simeon Pilgrim COAB reimplementation / fork** (cited in
[`../findings.md`](../findings.md) §4 as the de-facto DAX + ECL spec). Port its interpreter
logic; it gives the real opcode semantics, operand encodings, and the event model. Plan:

1. **Disassembler first.** Walk a block, decode opcode + operands into `EclOp[]`
   (`asset-model.md`), print a listing. Validate against COAB's disassembly of the same block.
2. **Interpreter.** Implement the opcode handlers (text output, conditionals, combat trigger,
   give-treasure, set-flag, jump/call, encounter, etc.) over an engine state object.
3. **Opcode drift.** CoK/DoK and DQK engines differ in opcode meaning/numbering (the same
   "ECL opcode drift" risk flagged for the DOS rebuild). The VM must be **ruleset-parameterized
   by the manifest** (`ruleset`/`engineGeneration`) so one interpreter serves both generations
   via per-generation opcode tables.

Event types (the high-level vocabulary the VM drives) are enumerated in `SCRIPT.TXT` /
`GEODATA.TXT`: Combat, Text Statement, Give Treasure, Damage, Stairs, Training Hall, Tavern,
Shop, Temple, Question, Transfer Module, Guided Tour, Add/Remove NPC, NPC Says, Encounter,
Sounds, Vault, Camp, Teleporter, Quest Stage, Special Items, etc. Useful as the target API
surface for the interpreter even before the bytecode is decoded.

Start with **non-combat scripted events** (text, questions, flags, transfers); add combat
last (it couples to the combat engine).

---

## 3. GEO maps — PARTIAL

The FRUA/DQK `geo*.dat` layout is fully documented in `GEODATA.TXT` and maps onto
`GameMap` (asset-model.md). Key offsets (DQK/FRUA lineage):

```
26  module height          27  module width
28-30 wall slots 1-3 (255 = none / overland)
31  area-view flag         32-35 backdrop slots (overland: 32=bigpic, 33-35=255)
36  dungeon combat art slot   37 wilderness combat art slot
38.. 8× entry points {col,row,facing,_}
70.. rest events,  102.. step events
142 module name   158-285 zone names (15ch+0, 6-bit packed)
322 map data: 576 × 6-byte cell records (walls + event#)
3786 encounter table: 100 × 20-byte event records (type byte + data)
5786 "strg": packed text boxes (lengths array + 6-bit strings)
```

**CoK/DoK DAX `GEO*` are structurally similar but NOT byte-identical** (older engine, smaller
maps, EGA art slots) — the offsets above are the DQK/FRUA shape; the CoK/DoK variant needs a
short reversing pass (cross-check against the COAB reimplementation, which reads CoK/DoK GEO).
Decode `width/height/slots/entries/events` first; the per-cell wall encoding (the 6-byte
record's bit packing into N/E/S/W) is the part to confirm against real bytes.

---

## 4. Monster & item tables — PARTIAL

**Monsters.** CoK/DoK split per area into `MON*CHA` (stats), `MON*ITM` (carried items),
`MON*SPC` (special abilities), `MON1WIZ` (mage spells); DQK packs `MONCHA.GLB`. The records
are **RLE-packed inside the container** (`MONSTDAT.TXT`: each record starts `01 C2`; runs are
the same signed-byte scheme — `>128` ⇒ repeat `257-c`, `<129` ⇒ `c+1` literals). After
RLE-expansion the per-game field layout (HP/AC/THAC0/attacks/icon slot) must be mapped; keep
`Monster.raw` and let the ruleset interpret. The combat-icon slot links to a `cbody`/`cpic`
`FrameSet`.

**Items.** 18-byte records (`ITEM.TXT`), 254 items (1..254). Notable fields: byte 0 = pointer
into `items.dat`; bytes 1-3 = name codes (read byte3→byte1, resolved via `VOCAB.TXT`);
4-5 encumbrance; 6-7 price (PP); 8 magic bonus; 11 identified-mask; 12 cursed; 14 charges;
15-16 magic effect/special code. CoK/DoK DAX `ITEM*` are close but per-game; map into `Item`,
keep `.raw`.

`VOCAB.TXT` is the static name vocabulary (1=Battle Axe, 2=Hand Axe, … 18=Long Sword, …) —
load once into `ItemTable.vocab` for name reconstruction.

---

## 5. Save format — TO-DECODE

`SAVGAM.TXT` documents the Gold Box save layout (party, character records, vault). Per-game
save extensions differ (CoK `.sav/.swg/.fx`, DoK `.gam/.eff/.jnk/.wiz`, DQK `.qch`). Not
needed until the engine is playable; document via SAVGAM.TXT when implementing save/load.
Character records reuse the same 6-bit name packing and item-record format above.

---

## Port path summary (recommended order)
1. 6-bit string unpack (trivial, unblocks names/text everywhere).
2. ITEM + VOCAB tables (small, self-contained, immediately useful in a viewer).
3. GEO decode for DQK (documented), then CoK/DoK GEO (short reversing pass vs COAB).
4. MON RLE-expand + field mapping.
5. ECL **disassembler**, validated against COAB.
6. ECL **interpreter**, non-combat events first, ruleset-parameterized for opcode drift.
7. Save format last.

Everything here is decoupled from rendering: these loaders emit the neutral
[asset-model.md](./asset-model.md) tables/scripts; the engine (layer 4) is the only consumer.
