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

### L24d2 full scope (read 2026-06-15) — why it needs mon verification

L24d2 is bigger than "roll 6 abilities". The full body:
- **roll loop** (confidently transcribable): per ability i=0..5, roll
  `jt870(3,6)+1` (=4..19) six times keeping the highest, apply a per-race ±1
  (JT[3] on rec[88]), store the current byte at rec[113+i*2], copy to the
  permanent byte rec[112+i*2]; i==0 also rolls exceptional-strength %
  (rec[124]=jt485(100)+1) and copies to rec[125];
- **L1672 racial/class clamp** (the 1.5K-line, NON-uniform part) — and the INT
  arm reads **rec[82] as an age/HP threshold** against -30612, contradicting the
  save-loader's "rec[82] = maxHP". That field-semantics conflict alone blocks a
  confident lift;
- **proficiency spread** over -16906 into rec[339..354] (like L3cd4);
- design-handle copies (rec[76]/78/80 via jt1199), jt885 (CODE 19 HP roll),
- then a **wait/reroll loop `while (!-22731) L6cd2()`** — L6cd2 = 0x6cd2 =
  *entry_jt557* (the train-screen address); why the roll pumps jt557 is unclear.

### LIFTED 2026-06-15 (the asm IS the ground truth)

The "rec[82] contradiction" dissolved on reading the asm: rec[82] is the **age**
during char-gen (compared to the -30612 age table) and l29ae later reuses the
same byte for max HP — both are just what the code does, no naming ambiguity
needed. So the roll was lifted straight from the disassembly, no mon required:
- **L1672** (l1672, b5b1780) — the per-ability racial/class min-max + aging
  clamp, all six arms transcribed with their exact -30960/-30612/-30552 offsets.
- **L24d2** (l24d2) — the roll loop: best of six jt870(3,6)+1 per ability, racial
  ±1 by rec[88] (race 0 +DEX -CON, race 2 +CON -CHA, race 4 -STR +DEX), L1672
  clamp, copy current rec[113+i*2] -> permanent rec[112+i*2], STR exceptional %.
  The Mac tail (jt885 HP, the jt557 reroll-wait pump) is deferred — jt574 already
  runs L29ae/L3cd4 on the committed record; jt895 (the per-ability display) is a
  stub.

NOT wired: l24d2 writes the FAITHFUL ability layout (rec[112+i*2] words), which
the port roster can't render until this very unification. cg_roll_stats stays
the live roll until step 3 below repoints CHAR_STATS -> 112 and jt574 threads
cg_rec (now stat-complete) into the pool.

NOTE the abilities are 6 **words** (permanent hi-byte @112+i*2, current @113+i*2,
both = the roll initially), not 6 contiguous bytes — the migration must store
them word-strided, and the savegame-loader translation must de-stride.

## Migration plan (staged, each step build + Hatari-verified)

0. **DONE 2026-06-15** — wired the faithful roll (l24d2/l1672) into jt574,
   reading the rolled scores out of rec[112+i*2] into the port stat struct;
   cg_roll_stats retired from the create path. Char-gen abilities are now
   faithfully rolled + racially capped. (Verify: create a character, stats are
   sane 3..18 with racial caps. The -30960/-30612/-30552 tables are confirmed
   inside the DATA range: G_A5_INIT_BYTES_LEN=31336 > 30960.)
1. **CHAR_INPARTY stays at 210** — the faithful record never touches offsets
   200..216 (verified: no a0/a1 reads there in the disasm), so the port flag is
   collision-free and round-trips through the .cch save as harmless padding. No
   relocation needed.
2. **DONE 2026-06-15 — overlay the faithful fields onto the pool record.** After
   cg_build_record, jt574 copies cg_rec's faithful char-gen fields onto the new
   pool record (race@88, class@89, gender@92, align@93, abilities+exc-str
   @112..125, icon@188, proficiency@339..354, maxHP word@82), so a port-created
   character is faithful ON DISK (jt578 saves the real offsets). The port
   display offsets (200..216) + shared combat stats (384..396) are untouched, so
   the roster is unchanged. NOTE this is intentionally a dual-layout step, not
   the offset repoint: repointing before the pool record IS cg_rec is premature,
   and CHAR_CLASS is a 6-value port encoding vs the faithful 0..16 — those are a
   model change, not an offset change.

### The remaining TRUE single-layout switch (its own focused effort)

To retire 200..216 entirely the pool record must BECOME cg_rec (faithful) and
~68 readers switch atomically (they can't stage — a reader on the old offset
reads 0 from a faithful record). That switch also needs: the CHAR_STATS readers
moved from `[CHAR_STATS+i]` to the word-strided `[112+i*2]`; the name tables
reordered to the faithful index order (race 0=Elf..5=Human from the racial-mod
switch + list positions; class to the 0..16 model; align order TBD from rec[93]);
CHAR_MAXHP widened to the word@82; and the HP/AC/THAC0/Move/level gaps in cg_rec
filled (l29ae HP + level 1 + the AC/THAC0/Move the deferred jt885 tail sets). It
is verifiable only in Hatari, field by field — best done as a dedicated pass.
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
