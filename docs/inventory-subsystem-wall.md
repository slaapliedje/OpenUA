# Inventory / item-list subsystem — findings + worklist

Goal: show each character's equipped items on the sheet (BasiliskII reference:
BARBARUS shows **LONG SWORD +1** / **PLATE MAIL +1** below DAMAGE), then make
the sheet's **ITEMS / TRADE / DROP** buttons functional. All of it reads the
same reconstructed item lists.

## What renders the equipment (traced, confirmed)

The char-sheet painter `l1276` (= jt886, CODE 19+0x1276) ends with:
```
jt898 (l19ac)            money/exp panel
jt892 (l1abe)            AC/THAC0/Damage/Encumbrance/Movement panel (ends at Damage row 19)
if (rec[12] != 0) jt28(rec, rec[12], slot 21)   <-- EQUIPPED items (rows 20/21)
if (rec[20] != 0) jt28(rec, rec[20], slot 22)   <-- effects
```
- `jt28` (CODE 6+0x0dc6, `a5@(258)`) is the list/item painter and **is lifted**.
- The port's `l1276` comment calls rec[12] "memorised spells" — that's WRONG.
  BARBARUS is a Fighter (no spells) yet the Mac paints weapon+armor via slot 21,
  so **rec[12] = the equipped-items list**. Spells are rec[4] (see jt577).

## Why nothing shows / why it crashes

`port_load_savgame` just `memcpy`s the 512-byte record. So:
- `rec[8/12/16/20]` are **stale Mac heap pointers** (e.g. BARBARUS rec[12] =
  bytes [246,8,147,87]). The item nodes are never deserialized.
- The sheet *did* render before only because the synthetic test party had
  rec[12]==0 (skipped). With the real party, rec[12]≠0 → `jt28` walks a garbage
  pointer. (Empirically: rebuilding rec[8] but leaving rec[12] stale → **Bus
  Error reading at $20** during the sheet paint.) So rec[12]/16/20 MUST be
  cleared and/or rebuilt before the sheet draws.

## The item serialization (same as the .cch format jt577 reads)

After the 398-byte fixed record, `rec[193]` item nodes are serialized inline
(this is why SAVGAMA records are variable-length). Confirmed for BARBARUS:
`rec[193] = 6`; bytes at rec+398 = `41,0,111,41,100,0,244,1,…` (the first item).
NIVLOC = 4 items.

`jt577` (.cch reader, CODE 15+0x3fe, port @ ~29035) is the faithful template:
- per item: allocate a **62-byte node** from the `-21508` pool (`jt477`), copy
  the **18-byte on-disk record to node+40**, byte-swap the words @44/@46
  (`jt1180`), clear node+0 (next) and node+58 (sub-list).
- `item[40] == 73` ('I') = a container holding `item[53]` sub-items, linked
  through node+58.
- spells: a separate list at **rec[4]** (10-byte nodes linked via node+6).

A standalone in-memory deserializer (mirroring this, reading from the record
copy instead of a file) was written + **validated** (parsed 6/4 items correctly)
but reverted because of the two blockers below.

## Blockers (must solve before wiring)

1. **Pool timing.** The `-21508` node pool (`-21504` base ptr) is allocated by
   `l311c` inside `l4cc0` — but `l4cc0` at boot (ua_main:~2011) leaves `-21504 = 0`
   (verified). It's only allocated later, on the design-load path. So the rebuild
   must be **deferred/lazy** (e.g. a done-flag guard, run once the pool exists —
   first in-game sheet paint, or the play-entry).
2. **rec[12] routing.** `jt577` only builds rec[8] (inventory) + rec[4] (spells).
   The sheet reads rec[12] (equipped) + rec[20] (effects). Need to find how the
   Mac populates rec[12]/16/20 — likely a re-equip/categorise pass over rec[8] by
   each item's location/worn flag (find that field in the 18-byte record), OR the
   SAVGAMA native load (NOT jt577) reconstructs them directly. Trace the Mac
   SAVGAMA character-load (not the .cch jt577) to get this faithful.

## Plan (next session)

1. Deserialize rec[8] (inventory) + rec[4] (spells) from the in-memory record,
   mirroring jt577. Run it lazily once the pool is up. Keep the full serialized
   tail (the 512-byte copy truncates >~6 items) — read items from the original
   save buffer, or stash the tail per character.
2. Trace the Mac's rec[12]/16/20 population (the equip/categorise step) and lift
   it, so jt28(rec[12]) walks a real list. Clear stale rec[12]/16/20 first to
   avoid the bus error.
3. Verify the sheet shows BARBARUS's LONG SWORD +1 / PLATE MAIL +1.
4. Then the ITEMS / TRADE / DROP handlers (`l25ce`/`l4334`/`l46e0` in jt904) on
   the now-real lists.
