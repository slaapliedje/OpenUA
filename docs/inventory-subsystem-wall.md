# Inventory / item-list subsystem — findings + worklist

Goal: show each character's equipped items on the sheet (BasiliskII reference:
BARBARUS shows **LONG SWORD +1** / **PLATE MAIL +1** below DAMAGE), then make
the sheet's **ITEMS / TRADE / DROP** buttons functional. All of it reads the
same reconstructed item lists.

## EQUIPPED-ITEM DISPLAY — FIXED 2026-06-26 (held pending Hatari test)

The whole load+name chain was already faithful and WORKING — a runtime
diagnostic (INVTRACE) proved every member's `rec[8]/12/16/20` populate after
the menu Load (jt579 -> jt577 deserialize + jt21 categorise), and `jt28` built
the correct names ("Long Sword" + "+1", "Plate" + "Mail" + "+1") from the
`-13628` name-part table. The ONLY break: `jt28` paints via `l3fd6`, and the
port had `l3fd6` as a **bogus duplicate PROBE stub**. `l3fd6` IS `JT[94]` (both
= CODE 6 + 0x3fd6, the engine text painter) — already fully lifted as `jt94`
(155 call sites). The Mac jt28 ends with a same-segment `jsr L3fd6`; the port
called the stub, so names were computed but never drawn. **Fix:** `l3fd6` now
forwards to `jt94` (`jt94(a,b,c,d,"%s",out)`). LESSON: when a CODE-local
`lXXXX` shares an address with a `JT[N]` export, it's the SAME function — don't
stub it; alias to the lifted `jtN`. (Same class as the jt-aliases trap.)
Remaining below = the ITEMS/TRADE/DROP handlers; the display is done.

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

## ITEMS / TRADE / DROP handlers — MAP (2026-06-26, jt904 submenu)

The View-character popup (jt182, labels from g_a5_-13804 = STRS 0x1db4 =
"Items Spells Trade Deposit Drop Lay Cure Exit") dispatches (JT[3] @ 0x232a):

| slot | label | handler | identity | size |
|---|---|---|---|---|
| 0 | Items | l25ce(out_done) | item browser + per-item sub-menu (JT[3]@0x28f8: Rdy/Use/Trade/Deposit/Drop/Halve/Join/Sell/Id/Exit) | 558 insns |
| 1 | Spells | jt595 | CODE-16 spell handler (lifted) | — |
| 2 | Trade | l4334 | trade items between chars | 236 |
| 3 | Deposit | l46e0(1) | deposit coins to vault (-27990==10) | 273 |
| 4 | Drop | l46e0(1) | drop coins (same arm, mode toggle) | (273) |
| 5 | Lay | l4f2c(char) | Paladin Lay-on-Hands (NOT import/export — fix that comment) | 58 |
| 6 | Cure | l4ff6(char) | Paladin Cure-Disease | 116 |
| 7 | Exit | — | loop ends | — |

MISSING helpers to create: l596a (target "…whom?" picker, shared by Trade/Lay/
Cure), l4264 (Trade), l35a0 (~1.5KB item-Join). Stub JTs in the arms: jt71/72/79
(item helpers), jt147, jt189 SELL/jt190 ID, jt423/jt483 (string width/len —
should alias l39ae). l4e56/l4ec6 gates are real (= Paladin ability checks).

CORRECTIONS to the agent map (verified 2026-06-26):
- `l3fd2` is NOT missing — it IS `jt891` (CODE 19 + 0x3fd2 = entry_jt891), the
  numeric "How much…" entry dialog, ALREADY LIFTED (boot.c ~63205, already
  called at 63345/63755). One less thing to lift.
- `l3f16` (treasure-type classifier: 'G'->Gems/1, 'P'/'S'->Platinum/0,
  'J'->Jewelry/2; writes the prefix, returns the code) LIFTED 2026-06-26
  (boot.c, before the l25ce stub block; marked unused until l46e0/l4334 call it).
  JT[1] @ 0x3f58 decoded with tools/jt1_extract.

SHARED-HELPERS lift status: l3fd2=jt891 ✓ (was already done), l3f16 ✓.
NEXT shared helper = l596a (the picker) — has a JT[1] nav table @ 0x5a90 +
deps jt153/jt162 (verify lifted vs stub) + jt937/jt179/jt163 (lifted). Then
the handlers (l46e0 Drop, l4334 Trade, l25ce Items).

LIFT ORDER (cheapest-first per the map): l596a -> l4f2c Lay -> l4ff6 Cure ->
l46e0 Drop/Deposit (+l3f16/l3fd2) -> l4334 Trade (+l4264) -> l25ce Items (+l35a0).
The "inventory" trio the user wants = l25ce/l4334/l46e0. jt904 dispatch is
already lifted (boot.c ~59789); only the handler BODIES are stubs (31231/31485).
Forward-decl note: handlers sit at ~31231 but many callees are defined later —
forward-declare or move the bodies down near jt904 (~59600).

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

1. **Pool timing — ROOT CAUSE FOUND + FIXED (2026-06-22).** `l311c` inside
   `l4cc0` *does* try to allocate the `-21508` pool (`NewPtr(640*62 = 39680)`),
   but at 4MB it returns **0**: only ~28KB is free by then because `jt463`
   (the FAR pool, `glib_pool_open` in `master_init`, BEFORE `l4cc0`) grabbed
   `FreeMem() - 32K` = its full 768KB cap, leaving nothing for `l4cc0`'s
   non-purgeable design buffers. The Mac got away with a 32K reserve because
   ITS design buffers were purgeable Handles. **Fix:** `jt463` now reserves
   256K (not 32K) so the post-pool design buffers fit; at 4MB the pool drops
   to 620KB (still > the ~461KB dungeon peak) and `NewPtr(39680)` succeeds.
   Verified: pool base non-zero, `jt579`/`jt577` deserialize the full 6-member
   HEIRS party, dungeon + event picture render, no bus error. The lazy/deferred
   rebuild is no longer needed — the pool is up by `l4cc0`. See
   docs/play-entry-wall.md.
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
