# MONCHA.GLB dec-path records — stat block survives in the decompressed member (C4.2b-2b-2b-ii)

Investigation date: 2026-06-22. Asset: `The Dark Queen of Krynn/DISK2/MONCHA.GLB`.
All byte offsets verified by offline TypeScript probes (deleted after use).

## Context

C4.2b-2b-2b-ii targeted the records the prior short-tail pass left failing: the SEA DRAGON
THAC0 anomaly, the 5 dragon "HEAD" sub-records, and the 8 records where `findAbilityBlock`
returns −1 even after `reconstructDqkRecord` (EYE OF THE DEP, GIANT ANEMONE, GIANT SQUID,
SEA SNAKE, GORGON, THENOL FNATIC, PRINCE ALHOOK, HUGE CROCODILE).

A probe over every failing record's post-name tail found **two distinct root causes**, only
one of which is recoverable from disk.

## Root cause 1 — `reconstructDqkRecord` *destroys* an already-expanded block (RECOVERABLE)

`decodeFullStatMonster` always tries the reconstructed record (a second `monchaRLE` pass over
the post-name tail). That pass is correct for records whose inner stat block was stored as a
literal run at the member level. **But some records carry their ability/stat block already
fully expanded in the decompressed member** — running `monchaRLE` again *re-compresses* those
bytes into garbage, so `findAbilityBlock(recon)` then fails or finds a false block.

For these, anchoring `findAbilityBlock`/`sniffAtAbility` directly on the **non-reconstructed
`dec`** bytes recovers the stats. Probe evidence (ability offset `A` in `dec`):

| member | name          | A (dec) | THAC0 | HD | dmg | AC | note |
|--------|---------------|---------|-------|----|-----|----|------|
| 16     | GIANT ANEMONE | 90      | 7     | 16 | 1d4 | 2  | recon@88 is 11 bytes (broken) |
| 17     | GIANT SQUID   | 39      | 9     | 12 | 1d6 | 3  | recon re-compresses to garbage |

These two were hand-verified. Adding the dec-path as a fallback (after the clean and recon
paths fail) then recovered **9 monster-framed records total** — the 2 above plus 7 more
non-type-8 records that were previously name-only, all passing the same strong stat gate
(THAC0 0–30, AC −12..16, HD 1–40, damage dice 1–12 × a valid die size):

GIANT ANEMONE, GIANT SQUID, FIRESHADOW, 2 HEADED TROLL, GHAST, WYNDLASS, SIVAK DRACONIAN,
ENCHANTED KAPAK (+ one more in the monster-framed set).

### Canon cross-checks (proves the dec-path reads real stat blocks, not in-range noise)

| record | decoded | AD&D canon |
|--------|---------|------------|
| GHAST  | AC 4, HD 4 | **exact** (MM Ghast AC 4 / HD 4) |
| GIANT SQUID | AC 3, HD 12, 1d6 | giant squid, plausible |
| SIVAK DRACONIAN | AC 1, HD 6 | Sivak AC 0–1 / HD 6+ ✓ |
| 2 HEADED TROLL | AC 4, HD 10 | troll AC 4 ✓ |

The character/NPC records with *rolled* ability spreads (BALDRIC, ELGYNORA, DAWNSHINE,
MINOTAUR MAGE, …) also surface a valid-looking block in `dec`, but the existing monster-framed
gate (`type ≠ 8 && !isMonsterDefaultAbility`) correctly rejects them, so they are **not**
misread as monsters.

Net: `decoded` 37 → **46** (29 → 38 monster-framed; NPCs unchanged at 8).

## Root cause 2 — genuinely unrecoverable / distinct variants (DEFERRED → C4.2b-2b-2b-iii)

The rest do not yield to the dec-path and are split off honestly:

| record | failure | recoverable? |
|--------|---------|--------------|
| SEA SNAKE | ability @35 (dec) valid for THAC0/HD/AC but **damage = 0** at the std delta | partial — damage location unknown |
| HUGE CROCODILE | the 12-byte run @39 (recon) is a **false** ability block (HD reads 248) | no clean block found |
| EYE OF THE DEP | **no** 12-byte [1,25]+`00 00 00` run in dec or recon | block fragmented |
| GORGON | **no** ability run at all (dec or recon) | block fragmented |
| SEA DRAGON | ability @40 found; THAC0 reads −2 (rec[A+15]=62), AC −4 / 1d12 valid | THAC0 at a different delta? |
| BLACK/BLUE/GREEN/RED/WHITE HEAD | run @39 (recon) but HD = 0 / THAC0 negative — a **false** block; likely dragon breath-/head sub-entries, not standalone combatants | probably not real monster records |
| THENOL FNATIC / PRINCE ALHOOK | run followed by `254`, not `00` — character-framed (NPC) | NPC path, not monster |

These need either a new ability-block heuristic (fragmented blocks), a damage-location trace
(SEA SNAKE), or confirmation that the HEAD records are sub-entries rather than monsters.

## Implementation

`decodeFullStatMonster` (`loaders/moncha.ts`) gains a dec-path branch after the recon-sniff
path and before the short-tail (HYDRA/UMBERHULK) fallbacks:

```
const Adec = findAbilityBlock(dec);
if (sniffAtAbility(dec, Adec)) {
  if (dec[type] !== TYPE_MONSTER && !isMonsterDefaultAbility(dec, Adec)) return null;
  return decodeDqkMonsterRecord(dec, memberIndex, { ability: Adec, name });
}
```

Only reached when both the clean (fixed-offset) and reconstructed paths fail, so no existing
decode changes. Gated identically to the recon path, so character/NPC records stay out.

## Sources

- `The Dark Queen of Krynn/DISK2/MONCHA.GLB` — primary asset (binary, verified)
- `packages/engine/src/loaders/moncha.ts` — decoder
- `docs/engine/research/moncha-shorttail-c4.2b-2b-2a.md` — prior pass (HYDRA/UMBERHULK)
- AD&D 2e Monster Manual: Ghast (AC 4, HD 4); Sivak draconian; troll
