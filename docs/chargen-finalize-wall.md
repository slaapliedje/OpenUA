# Char-gen finalize chain — worklist

## STATUS 2026-06-17: char sheet substantially DONE
`cg_level_from_xp` + `cg_finalize_stats` + `jt21` in `cg_char_sheet`.
Hatari-verified Human Fighter 10001 XP → LEVEL 4 / HP 27/27 / THAC0 17 / AC 8
(DEX) / DMG 1D2+2 (STR) / MOVE 12, matching the BasiliskII record.

- **Level** (8d4bc2f): XP-computed via per-class jt26 thresholds capped by
  -28048 (NOT a stored field — same machinery jt33 uses). RESOLVED.
- **HP** (2fe9a9d): jt885(gain=1) per level 1..N accumulates the hit dice + CON.
- **Race/class grants** (f9dc805): l2284 (racial abilities) + l13ee (class
  grants: Ranger ability 8, Magic-User spellbook 105; multi-class slot + XP
  split). Human Fighter gets none — matches the dump's gear-less fighter.

REMAINING: jt907 spell SLOTS (per-day counts for casters); multi-class
level/HP in cg_finalize_stats (single-class only now); HP rolls each reroll
(vs Mac once-after-Done). The real starting equipment for a fighter IS nothing
(buy with the 100 platinum) — the fist/AC-10/move-12 defaults are correct.

---


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
| **`l2284`/`l13ee`** | 17+0x2284/+0x13ee | **starting ITEMS per race/class** (jt876 = item/list-node builder → rec[4] list); l13ee also sets level slot rec[157+i]=1 (marker) + multi-class XP split (jt7) | **STUB → lift (items)** |
| **`jt907`** | 19+0x668e | spell slots / class abilities from level (tables -22836/-22980/-22788, jt40, jt388) | **STUB → lift** |
| `jt21` | 6 | AC rec[385]=f(base rec[179], DEX/armor); THAC0 rec[384]=rec[127](+l1e58, conditional, NOT auto-STR); dmg rec[389..]=f(weapon rec[173..], +STR l1f3e); move rec[396]=rec[136] | **LIFTED, not called in create → wire AFTER bases** |

### CORRECTIONS (earlier rows were wrong)
- **jt876 is NOT level-from-XP** — it's an item/list-node builder (adds starting
  equipment to rec[4]). So l2284/l13ee build the starting INVENTORY → base AC
  (rec[179] from armor) + weapon damage (rec[173..]).
- **THAC0 display is correct as base, no auto-STR** (confirmed: a L4 fighter is
  17; STR/DEX to-hit is applied per-weapon at attack time). jt21's l1e58 add is
  conditional — so wiring jt21 reproduces the Mac's 17 once rec[127] is right.
- **Level source UNRESOLVED.** rec[157+i] is set to 1 (a "class present" marker)
  by l13ee + l24d2; jt40 just returns that stored value (max with rec[164]).
  Ruled OUT as the level-4 source: l13ee, l24d2, l2284, jt40. jt572's per-class
  loop reads rec[157] to set base THAC0 rec[127] from table -23184[class*22 +
  level] — so it needs the real stored level. Where 10001 XP → stored 4 is not
  located. Candidates left: jt907, a design starting-char template, or an
  unfound XP-table lookup.
- **"LEVEL n" digit is painted outside jt886** (build-and-discard quirk) — the
  display path is also unresolved.

## The real shape

`AC/THAC0/damage = jt21(bases)`; bases = **level** (→ rec[127] THAC0) +
**starting items** (→ rec[179] AC, rec[173..] dmg) + **stats** (STR/DEX inside
jt21). So jt21 can't be correct until BOTH level and items are set — a connected
subsystem, not a one-function slice.

## CONFIRMED field map (BasiliskII mon, 2026-06-17)

Dump of a freshly-created Tutorial Human Fighter at the sheet (record @
0x1f6ce82, reached via long@0x904=CurrentA5 → A5-0x6D1C → ptr):

| Field | Off | Val | Notes |
|-------|-----|-----|-------|
| XP | 68 (long) | 10001 | design starting XP |
| platinum | 76 (word) | 100 | jt573 money |
| age | 82 (word) | 29 | cg_roll_age |
| encumbrance | 86 (word) | 100 | |
| race / class | 88 / 89 | 5 / **2** | Human / **Fighter = internal class 2** |
| base THAC0 | 127 | 42 | jt572 loop from -23184[class*22+level] |
| max HP | 129 | 42 | jt885 |
| saves | 131..135 | 13,14,15,16,16 | jt906 |
| base move | 136 | 12 | |
| **LEVEL** | **157+class = 159** | **4** | level is per-class-slot, NOT rec[157] |
| weapon dmg | 173/175/177 | 1 / 2 / 0 | base 1d2 (fist) |
| base AC | 179 | 50 | unarmored (→10) |
| THAC0 final | 384 | 43 | 60-43 = **17** (jt21; +1 over base = l1e58) |
| AC final | 385 | 50 | \|50-60\| = **10** (jt21) |
| dmg final | 389/391/393 | 1 / 2 / 1 | 1d2**+1** (jt21 adds STR dmg l1f3e) |
| cur HP | 395 | 42 | |
| move final | 396 | 12 | jt21 |

**KEY:** level lives at **rec[157 + class_index]** (rec[159] for class-2
Fighter). jt40/jt572-loop already key off the class index — they just need a
non-1 level. Everything downstream (jt572 loop → base THAC0; jt21 → final
AC/THAC0/dmg/move) already works once rec[157+class] holds the real level.

**REMAINING UNKNOWN:** what sets rec[157+class]=4. l13ee sets it to 1; l24d2
clamps to 1 at roll start. The level-4 write isn't found by grep on
rec[157/159/161] in the create path → either a design starting-level field, or
a post-roll level-from-XP recompute not yet located. (XP 10001 == fighter L4
band 8000-15999, so it IS XP-derivable.)

## Fastest unblock: a BasiliskII `mon` dump

A dump of a freshly-created Tutorial fighter's record settles it in one shot —
read: rec[68] (XP), rec[157..163] (levels), rec[127] (base THAC0), rec[179]
(base AC), rec[384]/rec[385] (final THAC0/AC), rec[173..177] (weapon dmg),
rec[4] (item-list head). That pins the level source + field semantics far faster
than tracing. Recipe in [[basiliskii-mon-3d-globals]].
