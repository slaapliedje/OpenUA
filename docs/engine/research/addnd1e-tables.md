# AD&D 1e ruleset tables — sources & confidence (`addnd1e`)

The Gold Box Krynn games run on **AD&D 1st edition** rules. This note records the canonical
tables the `addnd1e` plugin encodes, with a confidence label per block, so wrong constants are a
one-line table fix rather than a buried bug. Where confidence is below HIGH the value is *not*
asserted as a known-good golden in tests — only structural/spot checks are.

## Ability-score modifiers — **HIGH** (1e PHB)

`packages/engine/src/ruleset/addnd1e/abilities.ts`. Transcribed from the 1e Players Handbook
ability tables.

- **Strength** (melee hit/damage): 3 → −3/−1 … 16 → 0/+1, 17 → +1/+1, 18 → +1/+2, and the
  exceptional-strength bands 18/01–50 → +1/+3, 18/51–75 → +2/+3, 18/76–90 → +2/+4,
  18/91–99 → +2/+5, 18/00 → +3/+6; 19 → +3/+7.
- **Dexterity** (missile / defensive-AC / reaction): 15 → 0/−1/0, 16 → +1/−2/+1, 17 → +2/−3/+2,
  18 → +2/−4/+2; 3 → −3/+4/−3. Defensive AC adj is added to the descending-AC number (negative =
  better AC).
- **Constitution** (HP per HD): 15 → +1, 16 → +2, 17 → +2 (warriors +3), 18 → +2 (warriors +4);
  3 → −2. Non-warriors cap at +2; the warrior column carries the +3/+4 high-CON bonuses.

## Class XP / level progression — **HIGH** for fighter/cleric/magic-user/thief (1e PHB)

`packages/engine/src/ruleset/addnd1e/progression.ts`. Cumulative XP to reach each level up to
name level, then a flat per-level increment:

| Class | L1..name-level cumulative XP | per level after |
| ----- | ---------------------------- | --------------- |
| Fighter | 0, 2 000, 4 000, 8 000, 18 000, 35 000, 70 000, 125 000, 250 000 | +250 000 |
| Cleric | 0, 1 500, 3 000, 6 000, 13 000, 27 500, 55 000, 110 000, 225 000 | +225 000 |
| Magic-User | 0, 2 500, 5 000, 10 000, 22 500, 40 000, 60 000, 90 000, 135 000, 250 000, 375 000 | +375 000 |
| Thief | 0, 1 250, 2 500, 5 000, 10 000, 20 000, 42 500, 70 000, 110 000, 160 000, 220 000 | +220 000 |

Hit dice: fighter d10, cleric d8, thief d6, magic-user d4 (up to name level; flat +hp after).

**Deferred / lower confidence:** paladin, ranger, druid, illusionist, assassin, monk, bard
currently *alias* their nearest core group (paladin/ranger→fighter, druid→cleric,
illusionist→magic-user, etc.) so the API is total. Their own 1e XP tables (paladin and ranger in
particular differ and are higher) are **TODO** — transcribe before relying on those classes.

## Attack matrix (THAC0) / saving throws — **DONE (M2.S3b)** · spell slots **DONE (M2.S4b)**

Implemented in `ruleset/addnd1e/combatTables.ts` (`thac0`, `savingThrows`). Rather than reconstruct
from memory, the tables were **transcribed from the SSI engine itself** — the `simeonpilgrim/coab`
decompile (Curse of the Azure Bonds, the engine generation just before the Krynn games):
`engine/ovr018.cs thac0_table` and `engine/ovr026.cs SaveThrowValues[8][13][5]` — then
**cross-validated against Champions of Krynn's own data** (the "decode against an oracle" discipline):

- The CoK **NPC-class monster records** (`MON1CHA.DAX`) store each NPC's THAC0 (= `60 − byte[89]`).
  WARRIOR (fighter, HD 3) → 18, SOLDIER (fighter, HD 7) → 14, BLACK ROBE MAGE (HD 3) → 21 **exactly
  match** the transcribed table's fighter[3]=18 / fighter[7]=14 / magicUser[3]=21. This is a golden
  test (`combatTables.test.ts`), not an assertion — the ruleset table is proven against game bytes.
- The engine's `0x3C − hitBonus` THAC0 display (0x3C = 60) is the **same `60 − x` inversion** we
  independently recovered for monster THAC0/AC, so the convention is confirmed from two directions.

Tables are indexed `[class][level]`, level 0..12 (column 0 = the engine's "no class" placeholder;
levels > 12 clamp to the last column, where the engine caps). Saving-throw rows are `[PPD, PetPoly,
RSW, Breath, Spell]` (roll d20 ≥ value); multi-class characters take the best (lowest) per category.
Extra PC classes map to their 1e group (illusionist → magic-user saves/attack; assassin/bard →
thief).

**Spell slots — DONE (M2.S4b), `ruleset/addnd1e/spells.ts`.** `spellSlots(classId, level)` returns a
5-element `[L1..L5]` array. Built by **accumulating per-level delta rows** over a base 1st-level slot —
the same scheme the SSI engine uses (verified against coab `engine/ovr026.cs ClericSpellLevels` and
`engine/ovr020.cs MU_spell_lvl_learn`, accumulated in `calc_cleric_spells` / the magic-user branch).
This reproduces the canonical PHB tables exactly (magic-user L5 = 4/2/1, L11 = 4/4/4/3/3; cleric
L9 = 4/4/3/2/1). Cleric & druid use the cleric track; magic-user & illusionist the MU track; all other
classes get zero base slots. The **1e Wisdom bonus spells** are a separate ability-driven delta
(`clericWisdomBonusSpells(wis, level)`): Wis 13/14 → +1/+2 at L1, 15/16 → +1/+2 at L2, 17 → +1 L3,
18 → +1 L4 — each gated on the cleric already having a base slot at that level (matching the engine's
`spellCastCount[..] > 0` guard). 10 golden tests in `spells.test.ts`.

## Dragonlance overlay — **DEFERRED (M2.S4)**

`addnd1e-dragonlance` will layer CoK's class/race set (Knight of Solamnia orders, Kender, Tinker
Gnome), good-aligned party rules, and lunar/holy magic over this base via `RulesetPlugin.extends`.
