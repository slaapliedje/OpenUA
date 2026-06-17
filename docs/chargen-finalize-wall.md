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

## Fastest unblock: a BasiliskII `mon` dump

A dump of a freshly-created Tutorial fighter's record settles it in one shot —
read: rec[68] (XP), rec[157..163] (levels), rec[127] (base THAC0), rec[179]
(base AC), rec[384]/rec[385] (final THAC0/AC), rec[173..177] (weapon dmg),
rec[4] (item-list head). That pins the level source + field semantics far faster
than tracing. Recipe in [[basiliskii-mon-3d-globals]].
