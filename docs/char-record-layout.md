# Character record layout — faithful vs port, and the unification plan

Goal: retire the port-local `CHAR_*` stand-in offsets (200..216) so the whole
port (char-gen, roster, HUD, combat, save/load) operates on the **faithful
398-byte record** — the last step to "finish CODE 17". The serializer pair
(jt578/jt577) and char-gen (jt574 → jt566/567/568/L0006/L3cd4/L29ae) already use
the faithful record; the roster/play layer still reads `CHAR_*`.

## Confirmed faithful offsets (from lifted code + the disasm)

| field | offset | size | source |
|---|---|---|---|
| inventory list head | 4? / **8** | long | jt578/jt577 (rec[8]=inv, rec[4]=spells) |
| spell list head | **4** | long | jt578/jt577 |
| design/data handles | 68, 72 | long | L0ce0 (jt1199-swapped) |
| max HP | **82** | word | savegame loader; L0ce0-swapped |
| encumbrance | **86** | word | jt892; L0ce0-swapped |
| race | **88** | byte | jt566 writes `-7026 - 1` |
| class | **89** | byte | jt568 writes `-7018` |
| gender | **92** | byte | jt567 writes `-7020 - 1` |
| alignment (linear 0..8) | **93** | byte | jt569/570 → l2f74 |
| name | **96** | 15+NUL | jt892/jt584/cg_draw_sheet |
| ability scores (STR..CHA) | **112..117** | 6 byte | read in CODE 3/17/18/19 + savegame loader |
| max ability bonus | 127 | byte | jt572 derives |
| combination-class count | 162 | byte | L3cd4 (rec[162] tier) |
| per-class field (XP?) | 164+slot | ? | jt558, jt40 read rec[164] |
| bonus sum | 183 | byte | jt572 derives |
| body icon | **188** | byte | L0006 |
| inventory item count | 193 | byte | jt577 (rec[193]) |
| proficiency bitfield | 339..354 | 16 byte | L3cd4 |
| THAC0 (displayed = 60 - x) | **384** | byte | jt892 |
| AC (the "60 - shown" slot) | **385** | byte | jt892 |
| current HP | **395** | byte | jt892 |
| movement | **396** | byte | jt892 |

## UNRESOLVED — needs ground truth before migrating

These offsets are overloaded or contradictory in the disasm; getting them wrong
silently corrupts characters. The **BasiliskII mon** (dump a known character's
record — strongest verification, see [[mac-blit-ground-truth]]) is the way to
nail them:

1. **rec[157] — level or abilities?** jt40 (the level getter, CODE 6+0x2fd8)
   reads `rec[157]` AND `rec[164]` → reads as per-class level. But jt572 reads
   `rec[157+i]` for i=0..6 indexing the 22-byte/ability bonus table -23184 → reads
   as an ability-keyed value. Abilities are independently confirmed at 112. So
   157 is probably the **per-class level array** (slot 0..n), and jt572's use
   needs a second look. Mon: dump rec[112..117] and rec[157..163] for a known
   level-5 single-class character.
2. **XP offset** — "Experience:" displays in CODE 19 (STRS 0x5956); the long XP
   field offset isn't pinned (rec[164]? the per-class field). Mon: find the
   4-byte field holding a known XP value.
3. **maxHP is a WORD @82** but the port `CHAR_MAXHP` is used as a byte — the
   migration must widen the readers, not just repoint.

## CHAR_INPARTY has no faithful equivalent

The faithful game tracks party membership via the `-27928` linked list +
the `CHARxxxx.CHR` pool files, not a record byte. The port invented
`CHAR_INPARTY=210` (a real faithful field lives there). The migration must move
the port membership flag OUT of the record — either a parallel `port_inparty[16]`
array keyed by pool slot, or a pad byte ≥ 398 the faithful 398-byte record never
touches. (The pool buffers are 512B, so 398..511 is free port scratch.)

## The "ability roll" is NOT a prerequisite (corrected 2026-06-15)

Earlier framing said step 1 = "lift L34f0, the ability roll." Two corrections
after reading the disasm:

- **L34f0 is the pick-VALIDATION state machine** (re-derives valid options when
  a race/class pick changes, over the -30450 table) — not the roll.
- **The faithful ability roll is L24d2** (the jt870 3d6 / keep-highest loop +
  racial mods by rec[88], storing ability WORDS at rec[112+i*2]) -> **L1672**
  (~1,500-line per-ability racial-cap applier over -30612) + **jt895** (CODE 19,
  ~3,000 lines). A multi-thousand-line subsystem, its own multi-session effort.

Crucially it is **orthogonal to the layout unification**: the unification needs
the abilities at the faithful *offset* (rec[112+i*2], 6 words), not the faithful
*roll algorithm*. The port's cg_roll_stats can write into that faithful layout;
swapping in the lifted L24d2 is a later faithfulness improvement. So the
unification is NOT blocked on the roll — it's blocked on the field-map
confirmation above.

NOTE the abilities are 6 **words** (permanent hi-byte @112+i*2, current @113+i*2,
both = the roll initially), not 6 contiguous bytes — the migration must store
them word-strided, and the savegame-loader translation must de-stride.

## Migration plan (staged, each step build + Hatari-verified)

1. **Move CHAR_INPARTY** to a port-side array / pad byte (≥398).
3. **Repoint the confirmed, unambiguous fields**: CHAR_RACE→88, CHAR_CLASS→89,
   CHAR_ALIGN→93, CHAR_STATS→112. Drop the savegame-loader's faithful→port
   translation for these (it becomes a no-op). Verify the roster + a loaded
   HEIRS character still read correctly.
4. **Pin + repoint CHAR_LEVEL (→157 slot) and CHAR_XP (→?)** once mon confirms.
   Widen CHAR_MAXHP to the word @82.
5. **Make jt574 thread cg_rec (now complete) into the pool** instead of the
   port-struct cg_build_record; drop cg_build_record. Then char-gen →
   faithful record → roster → jt578 save → jt577 load is one faithful flow.
6. Audit the remaining `CHAR_*` readers + the test-party seed for stragglers.

## Status
- 2026-06-15: authoritative decode of the confirmed offsets (this file). Held
  off the offset repointing pending mon confirmation of the rec[157]/XP/maxHP
  ambiguities — repointing on a guessed map would corrupt the live roster/HUD/
  combat (68 CHAR_* sites). Steps 1–6 above are the staged execution.
