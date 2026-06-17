# Char-gen finalize chain — worklist

What actually computes a created character's data (vs. just rendering it). The
sheet panels (jt886/jt892/jt895/jt898) are DONE; this is the data feeding them.
The Mac applies the design's starting values + derived stats during char-gen;
the port currently computes only the ability roll + age, so HP/AC/THAC0/level/
spells/money are zero or garbage on a port-created character.

Reference: grep `docs/function-index.md` first; deep-dives in
`docs/function-reference.md` (char-gen section). Ground truth: the BasiliskII
create sheet (Tutorial new char = level 4, HP 42/42, AC 10, THAC0 17, XP 10001,
100 platinum, DAMAGE 1D2+1).

## Flow (faithful, L3b5e)

`jt574`: init record → `l3666` (pick; **jt572** Done handler is the finalize) →
`L29ae` sheet → name → body → `jt573` (body review + money) → save → proficiency.

The derived-stat finalize hangs off **jt572** (pick Done):
`l2284` → set XP rec[68] → `l13ee` → `jt907` (if rec[163]>0) → per-class level
loop → `jt906`.

## Worklist

| Fn | CODE | Role (record fields) | Status |
|----|------|----------------------|--------|
| `l24d2` | 17+0x24d2 | roll 6 abilities → rec[112..125] | **DONE** |
| `cg_roll_age` | 17+0x29ae head | starting age → rec[82] | **DONE** |
| rec[68] XP | jt572 | = g_a5_-18882 (jt1199 of design g_a5_-18844) | **DONE** |
| `jt573` money | 17+0x27cc | platinum/jewelry/gems → rec[76]/78/80 (design g_a5_-18840/-18836/-18832) | **DONE** |
| `jt906` | 19+0x687e | saving throws → rec[131..135] (jt40 + tables -28818) | **DONE** (lifted) |
| `jt885` | 19+0x55a2 | max HP → rec[129]/rec[82], cur HP rec[395] | **DONE** (lifted; called in jt573 — verify path) |
| **`l13ee`** | 17+0x13ee | **LEVEL from XP** → rec[157]/159/161/162 + rec[128]/160 (via jt876 level-from-XP, jt7) | **STUB → lift** |
| **`l2284`** | 17+0x2284 | multi-class level setup (jt876 ×≤9 over the class JT[3] switch) | **STUB → lift** |
| **`jt907`** | 19+0x668e | spell slots / class abilities from level (tables -22836/-22980/-22788, jt40, jt388) | **STUB → lift** |
| **`jt21`** | 6+? | derived stats: **AC rec[385], THAC0 rec[384]**, dmg rec[389/391/393], move rec[396], enc | **LIFTED but NOT CALLED in create → wire** |

Helpers (all lifted): `jt876` (level from XP, 18+0x1666), `jt7` (1+0x1ec),
`jt40` (class level), `jt1199` (endian swap), `jt388` (abs).

## Order of attack

1. **`l13ee`** — gives LEVEL (the visible "LEVEL n"); unblocks level-gated stats.
2. **`jt21` wired into the create finalize** — gives AC/THAC0/damage/move (the
   numbers the user flagged as "not real D&D"). It's already lifted; just call
   it after the levels/abilities are set.
3. **`jt907`** — spell slots (mage/cleric); lower priority than AC/THAC0.
4. **`l2284`** — multi-class level distribution; needed for multi-class chars.
5. Verify `jt885` HP runs on the create path (jt573) → HP 42/42.

Once 1–2 land, a port-created Tutorial fighter should read level 4 / AC 10 /
THAC0 17 / DAMAGE 1d2+x like the Mac.
