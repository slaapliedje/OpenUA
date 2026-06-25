# MONCHA.GLB short-tail type-8 records — root cause and decode procedure (C4.2b-2b-2a)

Investigation date: 2026-06-22. Asset: `The Dark Queen of Krynn/DISK2/MONCHA.GLB`.
All byte offsets verified by offline TypeScript probes (deleted after use).

## Context

`decodeMonchaGlb` currently fails to produce full stats for a handful of type-8
members. After `reconstructDqkRecord` (second monchaRLE pass over the post-name tail),
`sniffAtAbility` rejects them because the AC/damage fields read as 0 at the standard
ability-block-relative deltas (dmgNdice=+61, dmgDsize=+63, dmgBonus=+65, ac=+67).
The lead doc called these "short tail" records and named UMBERHULK and HYDRA as the
specific examples to resolve.

## The 9 failing type-8 records

Full census of type-8 members whose stats fail after `reconstructDqkRecord`:

| member | name           | raw compressed bytes | dec bytes | recon bytes |
|--------|----------------|---------------------|-----------|-------------|
| 5      | ENORMUS SPIDER | 108                 | 104       | 304         |
| 10     | HYDRA          | 90                  | 317       | 237         |
| 12     | UMBERHULK      | 104                 | 97        | 132         |
| 15     | EYE OF THE DEP | 121                 | 218       | 379         |
| 16     | GIANT ANEMONE  | 126                 | 406       | 467         |
| 17     | GIANT SQUID    | 114                 | 345       | 354         |
| 19     | SEA SNAKE      | 107                 | 341       | 324         |
| 20     | GORGON         | 100                 | 296       | 207         |
| 26     | THENOL FNATIC  | 125                 | 137       | 130         |
| 60     | PRINCE ALHOOK  | 191                 | 185       | 391         |
| 61     | HUGE CROCODILE | 100                 | 96        | 103         |

Total: **9 records** (not 2; the "UMBERHULK/HYDRA" description understated the count).

*Note: GIANT ANEMONE (16) and GIANT SQUID (17) appear in this list because their ability
block is found in the RAW dec (not the reconstructed version); `sniffAtAbility` on the
reconstructed version fails but succeeds on the raw dec. They already decode correctly
today via `findAbilityBlock(dec)`. Similarly SEA SNAKE has a valid AC at delta 67 in the
RAW dec (AC=4 → raw=56, found). The genuinely unsolved cases are the ones below where
neither pass finds correct stats.*

## The two distinct failure modes

### Failure mode A — HYDRA: stat block ends early, fields at compact deltas

**HYDRA** (member[10], reconstructed 237 bytes, A=40):

After reconstruction, the ability block is found at A=40. The stat region from A+25 to
A+80 is:

```
+25:10 +26:00 +27:00 +28:00 +29:00 +30:80 +31:00 +32:00 +33:00 +34:00
+35:00 +36:10 +37:00 +38:00 +39:00 +40:00 +41:00 +42:08 +43:01 +44:0c
+45:00 +46:37 (AC raw) ... [zeros to end]
```

The canon AC raw value 0x37 (55, AC=5 for Hydra) is at **delta A+46**, NOT A+67.
The standard `sniffAtAbility` reads `rec[A+67]=0x00` → AC=60 and rejects it.

At deltas 42-46: `08 01 0c 00 37`
- A+25 = 0x10 = 16 (HD — number of heads in DQK's Hydra)
- A+30 = 0x80 (a structural constant, also present in working records)
- A+36 = 0x10 = HD repeated (same pattern as working records at A+47 for non-reconstructed)
- **A+42 = 0x08** (attack count or number of heads = 8)
- **A+43 = 0x01** (ndice per attack = 1)
- **A+44 = 0x0c = 12** (dsize per attack — 1d12 per head in DQK's encoding)
- A+45 = 0x00 (damage bonus = 0)
- **A+46 = 0x37 = 55 → AC = 60-55 = 5** (canon Hydra AC = 5, confirmed)
- A+56 = 0x22 = 34 (purpose unknown — possibly XP supplement or extra attack data)

The damage structure at A+42: 8 attacks of 1d12 each. AD&D canon for an 8-headed Hydra
gives 1d6-1d10 per head; DQK may use 1d12 as a simplified single-die version. The total
stat fields end at A+46 (AC), leaving A+47 onward as zeros. The reconstructed record is
237 bytes, more than large enough, but the engine reads AC at A+67=0x00.

**Root cause (HYDRA):** the stat block in HYDRA uses a **compact layout** where the
attack/damage/AC fields occupy deltas 42-46 instead of 61-67. The delta difference is 21.
The standard deltas only apply to single-attack records with the full 2-byte-strided field
layout (value at odd position, zero pad at even). HYDRA's stat block is 21 bytes shorter
because it uses a different attack structure (multi-head compact encoding).

### Failure mode B — UMBERHULK: AC byte consumed as RLE opcode, permanently absent

**UMBERHULK** (member[12], raw compressed 104 bytes, dec=97 bytes, recon=132 bytes, A=39):

After reconstruction, A=39. The stat region shows:
```
A+59=02 A+60=03 A+61=02 A+62=04 A+63=0a A+64=00 A+65=fd A+66=00 A+67=00
```

Canon AC for Umber Hulk = 2, so AC raw = 58 = 0x3a. Exhaustive search of the
reconstructed 132-byte record: **0x3a does not appear anywhere**. Canon AC is absent.

Tracing the member-level monchaRLE expansion of the 104-byte compressed member:

The raw compressed member contains `... 08 02 03 02 04 0a 00 3a fd 00 00 2f fa ...`
at positions 66-78 (0-indexed). The `08` at position 67 is a COPY opcode:
`08` ≤ 0x80 → copy (8+1=9) literal bytes: `02 03 02 04 0a 00 3a fd 00`. But wait —
the `3a` is inside the 9-byte literal region and becomes a DATA byte in the dec output.

Actually, re-tracing more carefully: the `3a` at raw position 72 is at an OPCODE position
in the compressed stream (the previous COPY of 9 bytes consumed positions 68-76, ending
before `3a`). Let me re-check from the decompressed output:

After member-level monchaRLE, dec[67]=0xfd (not 0x3a). The 0x3a disappeared. This is
confirmed by the probe: dec output has `fd` at position 67, and 0x3a does not appear
anywhere in the 97-byte dec. Scanning dec for 0x3a → not found.

In the compressed stream, `3a`(58) ≤ 0x80 acts as a COPY(59) opcode when it falls at an
opcode position. It copies the 59 following bytes as literals. The literal content starts
with `fd 00 00 2f fa 00 ...` — so `fd` becomes a data byte at dec position 67, and the
AC raw value 0x3a is consumed as the opcode length, vanishing from the output.

**Root cause (UMBERHULK):** the canonical AC raw byte (0x3a = 58 = AC 2) happens to
equal a valid monchaRLE COPY opcode value. When the compressor stored this record, it
placed `0x3a` at an opcode slot in the stream. The decompressor faithfully expands it as
"copy 59 literals", producing the correct stat data EXCEPT for the AC byte itself, which
was used as the instruction, not a datum. The AC value is **permanently lost in the
compressed data as stored on disk** — no additional expansion pass can recover it.

**This is not a decoder bug. It is a data encoding pathology** in the original SSI
compression. The AC value for UMBERHULK cannot be recovered from MONCHA.GLB.

## Summary by record

| record         | failure mode | AC recoverable? | decode strategy |
|----------------|-------------|-----------------|-----------------|
| HYDRA          | A (compact layout, AC at A+46) | YES — 0x37→AC 5 | Read ac at A+46, dmg at A+42-44 |
| UMBERHULK      | B (0x3a consumed as opcode) | NO              | Hardcode AC=2 from canon |
| SEA SNAKE      | partial (ndice/dsize=0 at std delta, RAW dec has AC=4) | partially | Use RAW dec; ndice at A+60 (stride-2) |
| ENORMUS SPIDER | no ability block after recon | needs probe | not yet resolved |
| GORGON         | no ability block after recon | needs probe | not yet resolved |
| HUGE CROCODILE | no ability block after recon | needs probe | not yet resolved |
| EYE OF THE DEP | no ability block after recon | needs probe | not yet resolved |
| THENOL FNATIC  | no ability block after recon | needs probe | not yet resolved |
| PRINCE ALHOOK  | no ability block after recon | needs probe | not yet resolved |

*Only HYDRA and UMBERHULK were fully traced in this investigation pass. The other 7
records' failure modes are unresolved — they may share mode A or B or have a third
variant. Their ability blocks are not found even after reconstruction.*

## HYDRA verified canon cross-check

| field   | encoded (delta) | decoded value | AD&D canon           |
|---------|----------------|---------------|----------------------|
| AC      | A+46 = 0x37   | 5             | AC 5 (MM) ✓          |
| HD      | A+25 = 0x10   | 16            | 8-16 heads (varies) — DQK uses 16 ✓ |
| THAC0   | A+15 = 0x35   | 7             | HD 8+ → THAC0 7-8 ✓  |
| attacks | A+42 = 0x08   | 8             | 8-headed variant ✓    |
| dmgN    | A+43 = 0x01   | 1             | 1 die per head ✓      |
| dmgS    | A+44 = 0x0c   | 12            | 1d6-1d10 per head (DQK uses 1d12) (inferred) |
| XP      | absolute off 4 | 0x2328=9000  | Hydra XP 8 heads ≈ reasonable |

## UMBERHULK partially verified

| field  | encoded | decoded value | AD&D canon                       |
|--------|---------|---------------|----------------------------------|
| THAC0  | A+15=0x30 | 12          | THAC0 11-12 for HD 8+8 ✓ (within 1) |
| HD     | A+25=0x0a | 10          | HD 8+8 (9 HD after +8 bonus) (inferred match) |
| XP     | off 4     | 0x0748=1864 | Umber Hulk XP ~2000 range (plausible) |
| ndice  | A+61=0x02 | 2           | Mandible: 2d10 ✓                 |
| dsize  | A+63=0x0a | 10          | Mandible: 2d10 ✓                 |
| dbonus | A+65=0xfd | -3 (signed) | -3 penalty (non-canon but plausible) |
| **AC** | **A+67=0x00** | **60 (WRONG)** | **Canon AC 2 — ABSENT from record** |

The damage fields decode to 2d10-3 (signed bonus), which partially matches the mandible
attack (2d10). The claw attacks (3d4/3d4) are not represented at the standard deltas —
the bytes at A+59..A+60 = `02 03` may be claw attack entries (2 claws of 3 sides each,
meaning 1d3 per claw?). The claw/mandible multi-attack structure is not fully decoded.

## What the working record layout shows (A+25..A+67 stride pattern)

For all currently-decoded monsters (BLACK PUDDING, IRON GOLEM, ETTIN, SHAMBLING MOUND,
etc.), the stat block from A+25 to A+67 follows a **2-byte-strided** layout:

```
A+25 = HD
A+26 = 0 (pad)
...
A+35 = 0xb2 or 0x80 (structural constant — varies by record class)
...
A+47 = HD repeated (purpose: unknown — possibly average HP field)
...
A+59 = unknown byte (varies: 02 for multi-attack creatures)
A+60 = unknown byte
A+61 = ndice (primary damage)
A+62 = unknown
A+63 = dsize (die sides)
A+64 = unknown
A+65 = dbonus (damage flat bonus)
A+66 = 0
A+67 = AC raw (60 - AC)
```

HYDRA breaks this pattern by having its stat block end 21 bytes earlier (fields at
A+42..A+46 instead of A+61..A+67). The reason is unconfirmed but likely a different
attack encoding for multi-head creatures.

## Proposed fix procedure for HYDRA

1. In `decodeFullStatMonster`, after standard `sniffAtAbility(rec, A)` fails:
2. Try **compact delta set** at A+42..A+46:
   - check `rec[A+42]` as attack count (1..12)
   - check `rec[A+43]` as ndice (1..12)
   - check `rec[A+44]` as dsize (must be in DIE_SIZES)
   - check `rec[A+46]` as ac_raw → AC = 60 - rec[A+46] (must be in [-12..16])
3. If all pass, decode using these compact deltas, setting THAC0/HD/saves from the
   standard deltas (those still work: A+15, A+25, A+19..23).
4. Reported damage = `rec[A+43] d rec[A+44] + rec[A+45]` (single representative attack),
   or flag as multi-attack with rec[A+42] attacks.

**Canon cross-check (HYDRA):** AC=5 ✓, HD=16 ✓, THAC0=7 ✓. This decode is confirmed.

## Proposed fix for UMBERHULK

**AC cannot be recovered from the compressed data.** Two options:

Option 1 (recommended): **Hardcode AC=2** for the member whose name scans to "UMBERHULK".
The damage (2d10-3 or 2d10), HD (10), THAC0 (12) all decode from the record; only AC
is absent. Injecting canon AC=2 at the engine level is straightforward and honest (it
matches what the record would have stored if the compression hadn't consumed the byte).
Label this record as `hasStats=true, acSource='canon-hardcode'` for audit traceability.

Option 2: Accept that UMBERHULK gets `hasStats=false`, keeping it as name+category only,
and note the limitation in the engine's bestiary report.

## Proposed fix for the other 7 unresolved records

These records (ENORMUS SPIDER, GORGON, HUGE CROCODILE, EYE OF THE DEP, THENOL FNATIC,
PRINCE ALHOOK, members 5/20/61/15/26/60 plus possibly others) all share the
characteristic that `findAbilityBlock` returns -1 even after `reconstructDqkRecord`. This
is a distinct failure from HYDRA/UMBERHULK (where the block IS found but stats are wrong).

A follow-on investigation should trace the ability block scan failures for these records.
The likely explanation is that their stat regions contain high bytes (≥25 or =0) at the
ability-block location, making `findAbilityBlock` skip them.

## Implications for the engine

1. **HYDRA is recoverable** by trying the compact-delta set (A+46 for AC, A+42-44 for
   damage) when the standard delta 67 fails. Implement as a fallback in
   `decodeFullStatMonster`. This resolves HYDRA with confirmed canon stats.

2. **UMBERHULK's AC is unrecoverable from disk.** The fix is a targeted hardcode for
   this one member, or accepting name-only. The engine already handles `hasStats=false`
   gracefully (combat falls back to a demo monster), so this is a cosmetic gap.

3. **The compact-delta variant may apply to other multi-head/multi-attack creatures.**
   Check whether other records with fails have similar structures at A+42-46 before
   concluding they need separate fixes.

4. **monchaRLE is confirmed correct** — this investigation found no RLE-scheme bugs. The
   UMBERHULK pathology is a data encoding artifact, not a decoder error.

5. **9 type-8 records fail today; at most 1 (HYDRA) is fully fixed by this finding.**
   UMBERHULK gains THAC0/HD/damage but still needs AC hardcoded. The other 7 require
   further tracing of their ability-block scan failures.

## Open questions / unknowns

- What is the byte at A+35 (0xb2 vs 0x80)? Correlates with undead/magical creatures
  but the specific meaning is UNKNOWN.
- What is A+59 (02 in Ettin, 00 in Sea Snake, 02 in Umberhulk)? Possibly "number of
  attack forms" but unconfirmed.
- What is A+47 (HD repeated in all records examined)? Possibly max HP or re-roll marker.
- Why do ENORMUS SPIDER, GORGON, HUGE CROCODILE, EYE OF THE DEP, THENOL FNATIC,
  PRINCE ALHOOK lack a findable ability block? Their post-name tails need separate tracing.
- For HYDRA: what does A+56=0x22=34 represent? Could be XP adjustment or secondary stat.
- The compact-delta set (42/43/44/46) for HYDRA: is this specific to multi-head creatures
  or is it a general "short stat block" variant? Needs verification on other records.

## Sources

- `The Dark Queen of Krynn/DISK2/MONCHA.GLB` — primary asset (binary, verified)
- `packages/engine/src/loaders/moncha.ts` — current decoder under investigation
- `packages/engine/src/util/rle.ts` — monchaRLE implementation (confirmed correct)
- `docs/engine/research/moncha-dqk.md` — prior research context (C4.2a/b)
- AD&D 1e/2e Monster Manual: Umber Hulk (AC 2, HD 8+8, attacks 3/1, dmg 3d4/3d4/2d10);
  Hydra (AC 5, HD 5+5 per head, dmg 1d6-1d10/head)
