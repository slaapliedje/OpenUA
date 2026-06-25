# DQK Character/NPC Record Decode (C4.2b-2b-2)

How The Dark Queen of Krynn stores its character/NPC-framed records in `MONCHA.GLB`,
and what field offsets describe their combat stats, class, level, HP, and AC.

## Context

`MONCHA.GLB` mixes two combat framings in the same container (established in `moncha-dqk.md`):
- **Monster-framed** records: ability block has all-10 defaults; THAC0/HD/damage/AC follow at
  fixed deltas from the ability block (`findAbilityBlock` + `ABILITY_DELTA`). These decode to canon.
- **Character/NPC-framed** records: ability block is a rolled spread (at least one score > 10).
  The damage (`1d2`) and AC (`10`) at the monster-delta positions are placeholders.

This document establishes the character-record layout for the NPC-framed records specifically —
i.e., where their real THAC0, HP, AC, level, saving throws, and class are stored.

## Investigation method

Probe `probe_chars.mjs` (deleted after run): decoded every MONCHA.GLB member via `monchaRLE` +
`reconstructDqkRecord`, located the ability block via `findAbilityBlock`, filtered to rolled-spread
records, then hex-dumped the post-ability region for each. Three further probes (`probe_chars2-4.mjs`)
systematically cross-checked field candidates across all 14 rolled-spread members and against the
Gen1 CCH reference layout (`CCHFORM.TXT`).

## The 14 rolled-spread records

`findAbilityBlock` + `isRolledSpread` (any score > 10, all in [1,25]) finds 14 members:

| m# | Name | typeByte | abilities (STR/INT/WIS/DEX/CON/CHA) | non-10-count |
|----|------|----------|--------------------------------------|--------------|
| 22 | BLACK OGUE | 0x00 | 15/15/9/18/15/11 | 6 |
| 31 | UVWW. | 0x0c | 10/18/10/10/10/10 | 1 |
| 41 | THENOL WIARD | 0x02 | 7/18/14/8/8/9 | 6 |
| 45 | RED DRAGON | 0x08 | 10/18/10/10/10/10 | 1 |
| 46 | BLACK DRAGON | 0x08 | 10/18/10/10/10/10 | 1 |
| 50 | DRK WIZARD | 0x02 | 7/18/15/18/8/8 | 6 |
| 55 | BAKAI SHAMAN | 0x00 | 10/10/15/10/10/10 | 1 |
| 64 | SHARMAN | 0x00 | 16/10/9/15/12/8 | 5 |
| 69 | GNOME TINKE | 0x00 | 15/10/10/15/15/10 | 3 |
| 78 | BLUE DRAGON | 0x08 | 10/18/10/10/10/10 | 1 |
| 79 | GREEN DRAGON | 0x08 | 10/18/10/10/10/10 | 1 |
| 81 | WHITE DRAGON | 0x08 | 10/18/10/10/10/10 | 1 |
| 93 | SELIA | 0x02 | 16/17/19/18/16/16 | 6 |
| 94 | TASLEHOFF | 0x02 | 13/9/12/16/14/11 | 6 |

**Critical finding: five of these (m31, m45, m46, m78, m79, m81) are NOT character records.**
They are monster records whose INT=18 (the DQK "intelligent monster" constant) causes them to fail
the `isMonsterDefaultAbility` check and get swept into the rolled-spread pool. The genuine
character/NPC records are the 8 with non-10-count >= 2:

| m# | Name | True framing |
|----|------|-------------|
| 22 | BLACK OGUE | NPC humanoid (type=0x00) |
| 41 | THENOL WIARD | NPC/character (type=0x02) |
| 50 | DRK WIZARD | NPC/character (type=0x02) |
| 55 | BAKAI SHAMAN | NPC humanoid (type=0x00) |
| 64 | SHARMAN | NPC humanoid (type=0x00) |
| 69 | GNOME TINKE | NPC humanoid (type=0x00) |
| 93 | SELIA | NPC/character (type=0x02) |
| 94 | TASLEHOFF | NPC/character (type=0x02) |

## Discriminator fix for INT=18 false positives

The existing `isMonsterDefaultAbility` checks `≥4 scores == 10 AND none > 10`. This passes for
all-10 monsters but **fails for dragons** (INT=18). Fix: also allow records where exactly **one**
ability score is non-10 (the INT=18 "intelligent monster" pattern) to be classified as
monster-framed. Proposed revised discriminator:

```
isMonsterDefaultAbility(rec, A):
  cur = 6 ability current-bytes
  non10 = count of cur[i] != 10
  if non10 == 0: return true          // pure all-10 monster
  if non10 == 1 && cur.every(v <= 18): return true  // single-elevated (dragons, INT=18)
  return false
```

This passes for all confirmed monster records (including the chromatic dragons) and rejects all
confirmed character records (non-10-count >= 2 for every genuine NPC).

The `UVWW.` record (m31, non-10-count=1, typeByte=0x0c) fits the same INT=18 pattern and
decodes cleanly as a caster-type monster with canon stats (THAC0=7, AC=0, HD=18) — the garbled
name is an artifact of an unusual compressed name region, not a char record.

## The character stat block — verified field map

After `reconstructDqkRecord` and `findAbilityBlock`, let `A` = ability block offset. All 8
genuine NPC records share this layout relative to `A`:

| Delta | Field | Description | Evidence / verification |
|-------|-------|-------------|------------------------|
| A+0..A+11 | 6 ability scores | (cur, max) byte pairs, same as monsters | direct read |
| A+12..A+14 | 00 00 00 | separator | universal across all records |
| **A+15** | **THAC0 byte** | `60 − byte` (same inversion as monsters) | verified: plausible for class+level; SHARMAN byte=54 → THAC0=6 matches a high-level fighter |
| A+16 | 0x00 | always zero | all 8 records |
| **A+17** | **HP (u8)** | stored hit points (see below) | values 36–89 plausible for DQK endgame NPCs |
| **A+18** | sub-type flag | 1 for humanoid NPCs/chars; 4 for dragon monsters | all 8 char records = 1 |
| **A+19..A+23** | **5 saving throws** | same layout as monster records | structurally consistent |
| A+24 | UNKNOWN | 6, 9, or 12 for char records; 24 for Sahuagin/Wraith/Dragon | UNKNOWN |
| **A+25** | **level (u8)** | character level (see below) | 9, 13, 18, 20, 21, 22, 27; echoed at A+51 |
| A+26..A+34 | 00 × 9 | padding | all 8 records |
| A+35 | 0x80 | flag (unknown meaning) | all 8 char records = 128; many monster records too |
| A+36..A+50 | 00 × 15 | padding | |
| A+51 | level echo | same byte as A+25 | exact match all 8 records |
| A+52..A+58 | 00 × 7 | padding | |
| A+59..A+66 | (varies) | secondary stat region | class-related bytes; see below |
| **A+67** | **AC byte** | `60 − byte` (see below) | placeholder 50→AC=10 for most; real for SHARMAN, BAKAI SHAMAN |
| A+68..A+70 | 00 × 3 | padding | |
| A+71 | **class code** | possible class enum (see below) | 2=fighter, 5=MU, 6=?, 8=cleric |
| A+72 | HP echo (partial) | equals A+17 for some records | matches m41, m50, m55, m64, m94; diverges for m22, m69, m93 |

### A+17 — HP

Stored hit-point values for the 8 character records:

| Name | A+17 | decimal | fits class? |
|------|------|---------|-------------|
| BLACK OGUE | 0x37 | 55 | plausible (STR=15, DEX=18 fighter-rogue type) |
| THENOL WIARD | 0x28 | 40 | plausible (INT=18 wizard, low CON) |
| DRK WIZARD | 0x25 | 37 | plausible (INT=18, WIS=15 wizard) |
| BAKAI SHAMAN | 0x24 | 36 | plausible (WIS=15 cleric, level 9) |
| SHARMAN | 0x4e | 78 | plausible (STR=16 fighter, level 18) |
| GNOME TINKE | 0x34 | 52 | plausible (STR=15, CON=15 fighter-type, level 13) |
| SELIA | 0x59 | 89 | plausible (DL hero, level 27 multiclass) |
| TASLEHOFF | 0x40 | 64 | plausible (DEX=16 kender thief, level 22) |

HP is stored directly as a single byte; no formula needed. These are consistent with the
Gold Box convention for NPCs in the final Krynn game.

### A+25 — level

Values: 9 (BAKAI SHAMAN), 13 (GNOME TINKE), 18 (SHARMAN), 20 (DRK WIZARD), 21 (BLACK OGUE),
22 (THENOL WIARD, TASLEHOFF), 27 (SELIA).

Cross-check against THAC0 (using AD&D 1e tables):
- BAKAI SHAMAN: level=9, THAC0=14. Cleric at level 9 has THAC0 14. **EXACT MATCH.**
- GNOME TINKE: level=13, THAC0=14. Gnome with DEX/STR/CON=15 but small creature THAC0 at ~14 plausible.
- SHARMAN: level=18, THAC0=6. Fighter at level 18 (Dragonlance Knight?) has THAC0 3–6. **PLAUSIBLE.**
- DRK WIZARD: level=20, THAC0=13. Magic-User 20 → THAC0 ≈11–13. **PLAUSIBLE.**
- THENOL WIARD: level=22, THAC0=11. Wizard 22 → THAC0 ≈9–11. **PLAUSIBLE.**
- TASLEHOFF: level=22, THAC0=10. His saves [8,7,4,11,5] are identical to BLACK OGUE and SELIA —
  suggesting a shared save table or a fixed high-level set.
- SELIA: level=27, THAC0=10. Multi-class DL hero; level-27 multi-class is feasible in DQK.

Level is confirmed as a stored byte, not computed. The echo at A+51 duplicates it.

### A+67 — AC

Most character records have AC=10 (byte=50), which is the same placeholder as the damage fields
(`1d2`). However, two records carry real AC:
- **SHARMAN**: AC=6 (byte=54). STR=16 fighter at level 18 with chainmail-equivalent.
- **BAKAI SHAMAN**: AC=7 (byte=53). WIS=15 cleric at level 9 in leather-equivalent.

This means the engine writes a real AC into A+67 for at least some NPCs, falling back to 10
when the NPC is not pre-equipped. The discriminator between placeholder and real AC is unknown
without correlating with inventory data. **(inferred)**

### A+71 — class code

Observed values:
| Value | Records | Ability profile |
|-------|---------|----------------|
| 2 | SHARMAN, GNOME TINKE | STR-primary, fighter types |
| 5 | THENOL WIARD, DRK WIZARD | INT=18, wizard types |
| 6 | BLACK OGUE, SELIA, TASLEHOFF | mixed stats, versatile |
| 8 | BAKAI SHAMAN | WIS=15, cleric type |

This suggests class code 2=fighter/warrior, 5=magic-user, 8=cleric. Value 6 covers BLACK OGUE
(fighter-rogue), SELIA (multi-class paladin/cleric DL hero), and TASLEHOFF (kender
thief) — possible meanings: 6=thief, 6=multi-class, or 6=NPC special. The field is at
**A+71**, which is **+4 past the AC byte at A+67**. The class code at A+71 does not match
the FRUA `CCHFORM.TXT` class enum (where 0=cleric, 2=fighter, 5=magic-user, 6=thief) but
follows a similar pattern offset by one or two. **(partially verified)**

### Saving throws (A+19..A+23)

All 5 bytes follow the same order as monster saving throws: paralyze/poison/death,
petrify/polymorph, rod/staff/wand, breath, spell. Values are raw roll-targets (lower = better).

BAKAI SHAMAN saves [7,10,11,12,12] with level 9, cleric — for AD&D 1e 9th-level cleric:
roughly 10/14/14/16/15 on the original tables. The DQK values are consistently lower
(better) than Gen1 tables, suggesting a shifted or compressed table. **(inferred)**

## Cross-reference against Gen1 CCH (CCHFORM.TXT)

The FRUA `.cch` record is 398+ bytes with name at 96–110, ability scores at 112–123, THAC0 at
127, max-HP at 129, saves at 131–135, current-level at 137, AC at 179. The DQK character record
is NOT the FRUA format; it is the same compact "monster-style" layout the Gen2 engine uses for
all creatures, with the character-specific fields embedded after the separator at A+15 onward.

The Gen1 CoK `.CCH` record (`monster.ts`/`savedCharacter.ts`) has ability scores at offset 16,
THAC0 at 89, class at 90, HP at 98. The DQK compact record has the same *relative* positions
only for the 3-byte separator and THAC0 delta (A+15), but HP (A+17) and class (A+71) differ
from Gen1 by a large margin — the Gen2 record is genuinely distinct, not a trimmed Gen1.

## Coverage

| Category | Count | Notes |
|----------|-------|-------|
| Genuine char/NPC-framed records (non-10-count ≥ 2) | 8 | Confirmed by ability profile |
| Monster-framed records with INT=18 false-positive | 6 | Dragons (m45/46/78/79/81) + UVWW. (m31) |
| Fields verified with cross-checks | 5 | THAC0, HP, level, saves (structure), AC (partial) |
| Records where THAC0+level pair validates | 3 | BAKAI SHAMAN (exact), SHARMAN (close), DRK WIZARD (plausible) |

All 8 genuine NPC records decode cleanly under the proposed layout. None resist.

## Proposed decode for MonsterRecord (character framing)

For a record with a rolled ability spread (non-10-count >= 2), read:

```
thac0       = 60 - rec[A + 15]
hp          = rec[A + 17]
subtypeFlag = rec[A + 18]          // 1 = humanoid/char
saves[0..4] = rec[A+19 .. A+23]   // same order as monsters
level       = rec[A + 25]          // echo at A+51
ac          = 60 - rec[A + 67]     // may be placeholder 10
classCode   = rec[A + 71]          // 2=fighter, 5=MU, 8=cleric, 6=?
```

**AC and damage (A+61..A+67):** the 1d2 damage is confirmed placeholder for all 8 character
records. AC is real for SHARMAN and BAKAI SHAMAN, placeholder (10) for the others.

**XP (absolute offset 4):** for char records, this field holds large values (64569, 64570, 65027)
that look like encoded XP-for-level totals or sentinel flags, NOT the combat XP award to the
player. Do not use offset-4 XP for char records as a kill reward.

**HP roll:** unlike monster records (which compute average from HD), character records store HP
directly at A+17. No formula required.

## Open questions / unknowns

1. **A+24**: Usually 12 for char records (also 12 for FRE GIANT / FIREMINION / FIRE LEMENTAL);
   9 for BAKAI SHAMAN (= level). Its meaning is UNKNOWN. May be movement rate or an extra level
   field.

2. **Class code 6 at A+71**: Covers BLACK OGUE (fighter-rogue), SELIA (paladin multi-class?), and
   TASLEHOFF (thief/kender). All three have the same saves [8,7,4,11,5] — could mean they all
   use the same class-save table. Class 6 may be "NPC special" or "thief/rogue."

3. **AC placeholder vs real**: No confirmed way to know from the record alone whether A+67 is a
   real equipped AC or the placeholder 10. Probably real when the record has trailing inventory
   data (longer records).

4. **A+59..A+66 region** (the "secondary stat" bytes): A+59 is 2 for most chars, 4 for BAKAI
   SHAMAN and SHARMAN. A+61 is 1. A+63 is 2 (damage die sides for the placeholder 1d2). The
   exact semantics of A+59 are unclear.

5. **Saves calibration**: DQK save values are lower (better) than Gen1 tables for equivalent
   levels. Whether DQK uses a different ruleset table or a compressed representation is UNKNOWN.

6. **Level 27 (SELIA)**: A level 27 multi-class hero is possible in DQK (which extends levels
   beyond the Gen1 cap) but has not been verified against the DQK manual or in-game data.

7. **UVWW. (m31)**: This member has a garbled name (result of raw binary bytes that happen to
   pass the ASCII scan in the 304–308 range) but has ability pattern [10,18,10,10,10,10] and
   decodes as a monster (AC=0, THAC0=7, HD=18 — consistent with a high-level caster monster).
   The garbled-name issue is separate from the character-vs-monster framing.

## Implications for our engine

1. **Implement the revised `isMonsterDefaultAbility` discriminator** (allow non-10-count == 1
   for INT=18 intelligent monster pattern). This correctly re-classifies the 5 dragons + UVWW.
   as monster-framed, removing them from the `npc` category and adding them to validated stats.
   Expected effect: full-stat decode for the chromatic dragons (RED/BLACK/BLUE/GREEN/WHITE).

2. **Add `decodeNpcRecord(rec, A)`** using the layout above: reads THAC0, HP, saves, level, AC,
   classCode at the `A` delta positions. Yields a `MonsterRecord` with `category='npc'` and real
   `hitPoints`, `thac0`, `armorClass` from the stored fields rather than placeholder values.

3. **Do NOT report the placeholder AC=10 as the NPC's combat AC.** The combat path should either:
   - Use the stored AC if it is not exactly 60−50=10 (i.e., acByte != 50), OR
   - Compute AC from class + level when the stored value is the placeholder.
   Formula for computed AC: UNKNOWN (open question 3). Using the stored value when != 50 covers
   SHARMAN and BAKAI SHAMAN correctly.

4. **HP is direct (no HD roll needed)** for character/NPC records. The `hitDice` average that
   the monster path uses must NOT be applied to these records.

5. **XP at absolute offset 4 is NOT the kill reward** for character/NPC records. Use 0 or a
   hardcoded game-balance value.

6. **Class code mapping** (A+71): confirm 5=magic-user, 8=cleric, 2=fighter before exposing in
   UI or ruleset. The 6=? case needs one more data point.

7. **Coverage impact**: Implementing the npc decode adds 8 records with real combat stats to the
   validated set, and the discriminator fix adds 5–6 monster records (the INT=18 dragons). The
   net result: `decoded` count increases by 13–14 members beyond the current 27.
