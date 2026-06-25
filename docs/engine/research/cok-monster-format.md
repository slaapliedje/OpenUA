# CoK monster record format — `MON{n}CHA.DAX`

Champions of Krynn stores monsters in `MON1CHA.DAX` / `MON2CHA.DAX` / `MON3CHA.DAX` (plus
`MON*ITM` items and `MON*SPC` special abilities). Each **DAX block is one monster** — a fixed
**409-byte "character" record** (monsters are characters in the Gold Box engine, the .CCH family).
`MON1CHA` holds 26 monsters: BAAZ, BOZAK, KAPAK, SIVAK (draconians), GOBLIN, HOBGOBLIN,
HOBGOBLIN LDR, SKELETON, ZOMBIE, GHOUL, OGRE, HILL GIANT, MINOTAUR, RED/WHITE DRAGON, GIANT
RAT/CENTIPEDE/SNAKE/SPIDER, plus NPC types (WARRIOR, CLERIC, EVIL CURATE, BLACK ROBE MAGE,
SOLDIER, STRANGBOURN, KILDIRF).

The related FRUA `CCHFORM.TXT` describes a *later* .CCH variant whose first 4 bytes are a
next-character pointer; CoK's record instead starts with the name inline, so the FRUA offsets do
**not** transfer directly. The map below was derived by correlating real CoK bytes against known
AD&D monster values.

## Verified fields (decoded by `loaders/monster.ts`) — HIGH confidence

| Offset | Size | Field | Evidence |
| ------ | ---- | ----- | -------- |
| 0 | 1 | name length (≤ 15) | "GIANT CENTIPEDE" = 15 = 0x0f |
| 1..15 | 15 | ascii name, NUL-padded | readable in every block |
| 16..27 | 12 | 6 ability scores, 2 bytes each (current, max): STR, INT, WIS, DEX, CON, CHA | Ogre/Hill Giant STR 19; Giant Rat/Centipede/Skeleton/Snake/Spider/Zombie INT 3; Strangbourn 16/14/14/15/12/15; Soldier DEX/CON/CHA 15/18/18 |
| 89 | 1 | THAC0, inverted: `THAC0 = 60 − byte` | Goblin 60−39=21, Ogre 15, Hill Giant 12, Red Dragon 10 — matches the 1e/CoK to-hit-by-HD curve |
| 90 | 1 | category: 7 = monster, 6 = humanoid class, 5 = special NPC | all bestiary = 7; Warrior/Cleric/Curate/Mage/Soldier/Strangbourn = 6; Kildirf = 5 |
| 98 | 1 | hit points | Giant Rat 2, Goblin/Skeleton 4, Baaz/Ghoul/Zombie 9, Ogre 18, Hill Giant 36, Red Dragon 33, Minotaur 41 — all match HD |
| 208..212 | 5 | saving throws: paralyze/poison/death, petrify/polymorph, rod/staff/wand, breath, spell (roll d20 ≥) | Goblin 16/17/18/20/19, Red Dragon 7/8/9/7/10, Hill Giant 10/11/12/13/14 — textbook 1e monster save progression |
| 214 | 1 | hit-dice count (mirrored at 251) | Goblin 0 (1−1 HD), Zombie/Ghoul 2, Ogre 4, Hill Giant 8, Minotaur 9 (6+3), Red Dragon 11 |
| 269 | 1 | damage: number of dice | with 270/271 below |
| 270 | 1 | damage: flat bonus | Red Dragon +3, Ghoul +1, White Dragon +2, Sivak +2 |
| 271 | 1 | damage: die size (so **damage = 269 d 271 + 270**) | Goblin 1d6, Ogre 1d10, Hill Giant 2d8, Giant Rat 1d3, Hobgoblin 1d8, Minotaur 2d4, Giant Snake 3d6, Giant Spider 2d4 |
| 275 | 1 | armour class, inverted: `AC = 60 − byte` | Goblin 6, Hobgoblin 5, Skeleton 7, Zombie 8, Ogre 5, Hill Giant 4, White Dragon 3, Bozak 2, Red Dragon −1 — matches canonical AC (CoK-specific where it differs: Minotaur 4, Spider 0, Sivak 1) |
| 304..305 | 2 | experience-point award (u16 little-endian) | **the real CoK awards**: Giant Rat 7, Goblin 10, Skeleton 14, Hobgoblin/Zombie 20, Ghoul 65, Ogre 90, Kapak 105, Bozak 175, Minotaur 600, Hill Giant 1400, Red Dragon 1950 |

The saving-throw block is independently valuable: it is a **verified oracle for the 1e monster
save tables**, usable when filling in the ruleset's `savingThrows` (M2.S3b).

### The `60 − x` inversion (AC and THAC0)

CoK stores both AC (offset 275) and the monster's THAC0 (offset 89) as `60 − value`. This is the
engine's internal "to-be-hit" representation: a higher stored byte = harder to hit / better at
hitting. Recovering the familiar AD&D number is just `60 − byte`, which yields negative ACs for
dragons (Red Dragon AC −1 ⇒ byte 61) exactly as expected. This was the headline find of the
2026-06-21 correlation pass and is what unblocks the combat loop (M2.S5).

## Located but not yet pinned — MEDIUM/LOW confidence

- **Offset 207** — high-bit flag set on the *large* monsters (Hill Giant, Ogre, Minotaur, Red/White
  Dragon, Giant Snake = 0x82/0x83; small monsters = 1). Reads as a size/HD-class flag, not raw HD.
- **Offset 251** — duplicates the hit-dice count at 214 (current vs base HD?).
- **Offset 266** — small per-monster value (most melee = 8, Centipede 4, Goblin/Hobgoblin/Minotaur
  2); candidate movement rate or attack-animation id, **not** number of attacks (no clean #attacks
  field was found — CoK abstracts most bestiary monsters to a single melee attack/round).
- **Offset 283** — scales with power but is **not** XP (that is the verified u16 at 304); candidate
  movement or a second damage/icon field.

## Still unmapped (minor)

**Number of attacks, movement rate, special-attack codes** (paralysis/breath/level-drain/poison)
remain unmapped — the multi-attack/breath monsters (Ghoul, dragons) carry extra nonzero bytes in
the 227..272 region that a dedicated pass could separate, cross-checked against `MON*SPC`. None of
these block the core combat loop, which now has AC, THAC0, HD, HP, damage, saves, and XP.
