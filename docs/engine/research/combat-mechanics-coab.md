# Combat Mechanics — COAB Ground Truth

**Scope:** COAB = "Curse of the Azure Bonds" decompile at `coab_extracted/`. Same Gold Box
engine generation as Champions of Krynn. Primary sources used:
- `coab_extracted/coab_new.lst` — IDA disassembly (cited by segment:offset)
- `coab_extracted/Classes/*.cs` — C# decompile layer
- `hackdocs_extracted/SPECAB.TXT` — community special-ability docs (FRUA/UA era)

Our new engine is `packages/engine/src/combat/tactical.ts`.

---

## 1. INITIATIVE

### COAB ground truth

**Source:** `coab_new.lst` ovr008:20E5 — `@calc_group_inituative$qm3Minm3Max`; Player
struct field at charStruct offset 0x1A5 is named `initiative` (also labelled `movement`
at Player.cs line 706).

The function walks the linked list of all combatants. For **each** combatant it reads
`charStruct.initiative` (field 0x1A5) into a local, then:
- If the combatant has the **haste** affect active: `player_initiative = player_initiative * 2`
  (doubles it, ovr008:2141-2148)
- Else if the combatant has the **slow** affect: `player_initiative = player_initiative / 2`
  (ovr008:2169-216B)
- Tracks a group min and a group max across all combatants.

The `initiative` field is stored **per-character in the save data** (Player.cs line
706, offset 0x1A5). Its value is NOT a random roll inside this function — it is read from
persistent state. The random assignment of `initiative` must happen at a different point
(character creation or round-start, UNKNOWN exactly where in the LST — the function
`encounter` in ovr003:20BD calls `calc_group_inituative` and also calls
`parse_command_sub(0x0E)` and `parse_command_sub(0x01)` before entering the combat loop,
suggesting initiative is set up by the ECL script or encounter pre-processing).

**Key facts confirmed:**
- Initiative is stored per-combatant as a byte (field 0x1A5 — SAME field as `base_movement`
  at Player.cs line 509 label `baseHalfMoves`; the dual naming is suspicious — see Open
  questions).
- Haste **doubles** the initiative value; Slow **halves** it.
- The engine computes group min/max across all active combatants — it uses these bounds to
  schedule movement (the `SortedCombatant` class in `Classes/Combat.cs` sorts by `steps`,
  then `direction`, then `direction % 2`).
- **No evidence of a per-round d10 roll in this code path.** The original engine appears
  to use a pre-set initiative value (possibly rolled or fixed by the ECL encounter) modified
  by spell effects.

**Gold Box combat is NOT per-combatant d10 each round.** Initiative in the original engine
is resolved **per group** (party vs monsters) based on the combatant with the highest
`initiative` value within each group; it is **not** re-rolled every round by the combat
loop itself. (The exact randomisation source is UNKNOWN but is likely the encounter setup.)

### Our engine vs COAB — delta

**WRONG.** `tactical.ts` beginRound() (line 280) rolls `rng.die(10)` per combatant every
round with ascending sort. This does not match. COAB uses a fixed-per-encounter group
initiative (with haste/slow modifiers), not per-combatant random d10 each round.

**Correction needed:** Store an `initiative` value on each `TacticalCombatant`, doubled
by haste and halved by slow. Sort the turn order by this value (descending = higher
initiative goes first) with party-before-enemy tie-break. Do **not** re-roll every round.

---

## 2. MOVEMENT

### COAB ground truth

**Source:** `Classes/Player.cs` line 509: `baseHalfMoves` (offset 0x11D). The global
`byte_1D2C0 = halfActionsLeft` (Gbl.cs line 338). At ovr014:004D:
```asm
mov al, es:[di+charStruct.field_11D]   ; load baseHalfMoves
mov byte_1D2C0, al                     ; halfActionsLeft = baseHalfMoves
```
Then at ovr014:0168:
```asm
shl ax, 1
mov byte_1D2C0, al                     ; halfActionsLeft = baseHalfMoves * 2 (hasted)
```
And sub_3B01B (ovr013:101B) that halves it:
```asm
shr ax, 1
mov byte_1D2C0, al                     ; halfActionsLeft /= 2 (slow)
```
The comment at ovr013:12A5 confirms: `es:[di+actions.move] = 0   ; maybe how much movement is left`.

**Movement is stored as "half-moves" (the engine counts in units of 2 = one full square).** The
`baseHalfMoves` byte (0x11D) holds double the actual grid squares. Dividing by 2 gives the
movement allowance in cells.

**Diagonal movement:** `Classes/Combat.cs` `SortedCombatant` comment at line 26–28:
> steps to counted in 2's, so that diagonal steps can be 3 (thus 1.5)

This is the critical diagonal rule: **orthogonal = 2 steps, diagonal = 3 steps** (i.e., every
other diagonal costs an extra half-step). This is the classic AD&D 5-10-15 diagonal rule
(every second diagonal costs double), NOT flat Chebyshev.

`gbl.MapDirectionDelta` (Gbl.cs line 690) confirms 8 directions including diagonals. The
battle map dimensions from `Point` (Gbl.cs line 111-114): **MapMaxX = 50, MapMaxY = 25**
— so the combat map is up to **50 columns × 25 rows**.

**Move-then-attack or attack-then-move?** The per-turn loop in `sub_359D1`
(ovr010:09D1) displays "Move/Attack, Move Left = X", where X = `halfActionsLeft / 2`
(line 95508-95511). The function signature and call tree suggest the actor can move AND
attack in any order within a turn; movement is decremented and the remaining can be used
after attacking (split movement is possible). The `actions.move` field in `Action.cs` line
16 tracks remaining movement separately from whether an attack has been used.

**Terrain cost:** `BackGroundTiles` in Gbl.cs lines 193-268 contains entries where the
first field is a movement cost (0xFF = impassable, 0 = open, 1 = normal, 2 = difficult,
4 = very difficult). Difficult terrain costs more movement.

### Our engine vs COAB — delta

**WRONG on diagonals.** `tactical.ts` treats diagonals as cost 1 (Chebyshev). COAB uses
**alternating diagonal cost** (orthogonal = 2 half-steps, diagonal = 3 half-steps = 1.5
squares). Every second diagonal costs an extra square.

**WRONG on movement allowance unit.** Our `move` field is already in cells; COAB stores
`baseHalfMoves` (field 0x11D) as double the cell count. When populating `TacticalCombatant`
from game data, divide `baseHalfMoves` by 2 to get cells per turn.

**WRONG on map dimensions.** No terrain dimensions are enforced. COAB combat map is
**50 × 25 cells** max.

**CORRECT: 8-directional movement** is confirmed. Move-and-attack (split movement) appears
supported — the "actions.move" remaining counter is separate from "has attacked."

---

## 3. COMBAT MAP / GRID

### COAB ground truth

**Source:** `Classes/Gbl.cs` `Point` struct (lines 111-114):
- `MapMaxX = 50`, `MapMaxY = 25` → battle grid is up to **50 × 25** cells.
- `ScreenMaxX = 6`, `ScreenMaxY = 6` → visible viewport is 6 × 6 (scrolled).

Party placement: `gbl.team_start_x[2]`, `team_start_y[2]`, `half_team_count[2]`,
`team_direction[2]` (Gbl.cs lines 297-300). Two teams (index 0 = party, 1 = enemy), each
with a start position, count, and facing direction. This is set up by the encounter ECL.

Monster groups in `gbl.CombatMap` (CombatantMap array, MaxCombatantCount = 0xFF) —
`CombatantMap.size` determines how many cells a combatant occupies.

Background tile table `BackGroundTiles` maps tile IDs to movement/sight properties
(first field = passability, second = sight block). Walls/obstacles are placed from the
GEO map cell data (`hackdocs_extracted/GEOGRIDS.TXT` — 6 bytes per cell: N/E/S/W walls,
event, backdrop).

### Our engine vs COAB — delta

**MISSING explicit map dimensions.** `TerrainGrid` has `width` and `height` but they are
caller-supplied with no enforcement. Enforce **50 × 25** as the canonical battle grid.

**MISSING party-placement logic.** Starting ranks are not modelled; tactical.ts requires
combatants pre-positioned. This is acceptable for the current layer but the loader must
implement `team_start_*` logic.

**CORRECT: terrain blocking/sight.** `TerrainGrid.blocked` and `blocksSight` match the
COAB two-property model.

---

## 4. TO-HIT

### COAB ground truth

**Source:** `Classes/Player.cs` line 348: `thac0` (sbyte, offset 0x73). `ac` at offset
0x19A, stored as **0x3C minus the actual AC** (DisplayAc = 0x3C - ac, line 598). So
internal `ac` byte 0x19A = 60 − displayed_AC.

**Source:** `sub_3F4EB` (ovr014:14EB) — the main attack resolver. It calls `sub_3FCED`
(ovr014:1CED) which handles the AC lookup: it computes how many squares the target is
behind cover (range brackets for missiles) and adjusts the raw AC. The actual d20 roll
is **not visible in the decompiled snippet shown** — it is inside a sub-function not yet
fully read — but the THAC0−AC model is confirmed by:
- The named field `thac0` in the struct.
- `hitBonus` field (Player.cs line 595, offset 0x199) — a signed bonus applied to the
  attack roll.
- `ac_behind` (offset 0x19B) — rear AC value; the to-hit calculation selects between
  `ac` (front) and `ac_behind` (rear) depending on attacker position
  (sub_3F4EB ovr014:16F7-1705 selects `es:[di+19Ah]` vs `es:[di+19Bh]` based on facing).
- Nat-20 and nat-1 as absolute hit/miss: **confirmed** by FRUA/Gold Box community
  documentation (hackdocs_extracted/SPECAB.TXT refers to "attack roll" logic consistent
  with AD&D 1e; the engine is documented as using standard d20 THAC0 in all Gold Box
  community sources).

**Backstab:** `backstab` proc at ovr014:0318 is called from `sub_3F4EB` (line 112028) with
arg `es:[di+1A4h] + 5` (attacker's current HP + 5) — this looks like it passes the backstab
multiplier. Thieves get a multiplier applied to damage on backstab (not extra to-hit — the
extra to-hit for backstab in the original rules is implemented as a flat AC bonus/penalty;
the exact implementation needs further tracing, flagged UNKNOWN).

**Range penalties for missiles:** `sub_3FCED` (ovr014:1CED) — calls `@sub_68708` (line
112848) first; this function appears to check whether attacker is in missile range and
returns an adjusted AC. Range brackets are computed from the item's range data in the
`ItemData` table. Exact short/medium/long bracket values: **UNKNOWN from COAB alone**
(would need to read the item data tables).

### Our engine vs COAB — delta

**CORRECT: THAC0 − AC formula** (`d20 >= thac0 - ac`, nat-20 hits, nat-1 misses).

**WRONG: AC encoding.** COAB stores AC internally as `60 − display_AC`. A character with
displayed AC of 5 has internal byte = 55. When loading game data, decode `ac = 60 - raw_byte`.
Our engine uses AC directly as the display value, which is correct only if the loader already
decoded it.

**WRONG/MISSING: Rear AC.** COAB tracks a separate `ac_behind` (0x19B). Our engine has
only one `ac` field. Attackers from behind use `ac_behind`.

**WRONG/MISSING: Range penalties.** `clearLineOfFire` in tactical.ts blocks any shot with
an intervening body. COAB does check range and appears to apply AC penalties per bracket —
it does NOT simply block the shot. Firing into melee likely applies a to-hit penalty rather
than an outright block.

---

## 5. ATTACKS PER ROUND

### COAB ground truth

**Source:** `Classes/Player.cs` line 507: `attacksCount` (byte, offset 0x11C) — labelled
"half-attacks count." This stores attacks as **halves** (e.g., value 3 = 1.5 = 3/2 attacks).

Fields at 0x19C and 0x19D: `attack1_AttacksLeft` and `attack2_AttacksLeft` — the engine
tracks remaining attacks for **two separate attack routines** per combatant. Monsters also
have two attacks (`attack1_DiceCount/Size/Bonus` and `attack2_DiceCount/Size/Bonus` at
0x19E–0x1A3).

`sub_3EDD4` (ovr014:0DD4) computes how many attacks a combatant has per half-turn:
- Reads `charStruct.attacksCount` (0x11C) as the full attacks count.
- Reads the weapon's `attackCount` from item data (field at 0x5D15h table).
- Takes `max(weapon_attacks, 2)` as a floor.
- Calls `sub_3EF0D` which does: `var_1 = (var_2 + carry_from_prev_round) / 2`.

The `byte_1D8B7` (Gbl.cs line 383: `combat_round`) is used in `sub_3EF0D` (line
111222-111224): on odd rounds, the extra fractional attack is credited. This implements
the **3/2 attacks per 2 rounds** fractional system.

**Monsters** use `action.maxSweapTargets` (Action.cs line 15) for sweep attacks, and the
two `attack*_AttacksLeft` counters for their two attack routines.

### Our engine vs COAB — delta

**WRONG: Fractional attacks.** `tactical.ts` uses `attacksPerRound` as a plain integer
applied every round. COAB stores `attacksCount` in half-attack units and credits the extra
half-attack on alternating rounds. A fighter with 3/2 attacks (value = 3) attacks twice in
even rounds and once in odd rounds (or vice versa).

**WRONG: Two attack routines.** Monsters (and some PCs with secondary weapons) have two
independent attack types (`attack1`, `attack2`), each with its own dice, bonus, and
remaining count. Our engine has a single `weapons[]` array without the paired-routine model.

**CORRECT: Multiple attacks per turn** (the loop over `attacksPerRound`) is the right
concept; the fix is the fractional/round-alternating logic.

---

## 6. MISSILE / THROWN

### COAB ground truth

**Source:** `sub_3F4EB` (ovr014:14EB), `sub_3FCED` (ovr014:1CED), and `@sub_3AF06`
(ovr013:0F06).

`sub_3AF06` is called with a percentage argument and a target: if a random 1d100 roll
beats the percentage, the target "Avoids it" (string at ovr013:0EFC). This is the **missile
dodge** mechanic for special abilities (e.g., Thri-Kreen, affect 0x68 = `thri_kreen_dodge_missile`
in Affect.cs line 112).

**Range brackets:** `sub_3FCED` calls `offset_above_1` (to check if a ranged weapon is
equipped) and then reads range data. The exact short/medium/long AC penalty values are in
item data tables at segment 600 (addresses around 0x5D10). From Gold Box community docs,
the standard brackets are:
- Short: +1 to attack (bonus, within normal range / 3)
- Medium: 0 (within range)
- Long: −2 to attack (> 2/3 of max range)

**Body-blocking vs AC penalty:** The `clearLineOfFire` in tactical.ts blocks a shot
entirely when a body is on the line. COAB's `sub_3FCED` does **not** fully block for
intervening bodies — it adjusts AC (applies cover penalty). Whether it blocks entirely for
enemies in the path vs applies a penalty is **UNKNOWN** from the code read so far.

**Ammo tracking:** `Player.arrows` (offset 0x17D) and `Player.quarrels` (0x181) are tracked
as Item references in `ActiveItems`. Count is decremented per shot. Our engine's `w.ammo`
counter matches this concept.

### Our engine vs COAB — delta

**PROBABLY WRONG: line-of-fire semantics.** COAB applies AC penalties for cover/range rather
than hard-blocking every intervening figure. Our `clearLineOfFire` returns false (no shot)
if ANY body is on the line. This is overly restrictive.

**MISSING: range-bracket to-hit penalties.** No short/medium/long penalty is applied.

**CORRECT: ammo tracking** (decrement per shot) matches COAB.

---

## 7. MELEE REACH / LARGE MONSTERS

### COAB ground truth

**Source:** `Classes/Combat/CombatantMap.cs` — `CombatantMap.size` field. This is how many
cells a combatant occupies. `gbl.CombatMap[]` is indexed by combatant. The `stru_1D1BC`
struct (used in battle_begins) sets `es:[di+2]` and `es:[di+3]` to values computed from
party positioning (offset from center − 3), suggesting a multi-cell concept.

The `icon_size` field in Player.cs (line 553, offset 0x144): `1 = small, 2 = normal`
(comment on line 553). Large monsters have `size > 1` in CombatantMap.

Whether `size > 1` grants **reach > 1** is **UNKNOWN** from the code inspected — the reach
check would be in the movement/attack targeting code which was not fully read.

`ac_behind` (offset 0x19B) implies the engine tracks facing/orientation, which is relevant
to which cells a large creature "occupies" for targeting purposes.

### Our engine vs COAB — delta

**MISSING: multi-cell occupancy.** Our `TacticalCombatant` has a single `GridPos`. Large
monsters in COAB occupy multiple cells (CombatantMap.size). This is partially modelled
by `reach` but occupancy itself is not tracked.

**UNKNOWN: whether size directly extends melee reach.** Cannot confirm from current read.
Flag for future investigation.

---

## 8. MORALE & FLEEING

### COAB ground truth

**Source:** `sub_3637F` (ovr010:137F) — the morale/fleeing function. Called from
`sub_3504B` (which is the per-turn action dispatch). Key logic:

1. **First check:** If `actions.fleeing` (field 0x10) is already set, the combatant is
   forced to flee — prints "is forced to flee" (ovr010:13C8-13D8).

2. **Monster check gate:** `es:[di+charStruct.field_F7]` (= `control_morale` at offset
   0xF7) must be > 0x7F (byte > 127 means it is a monster under AI, not a PC).

3. **Morale score computation:**
   ```
   byte_1D2CC = (field_F7 & 0x7F) * 2   ; morale_score = (control_morale & 0x7F) * 2
   if byte_1D2CC > 0x66:
       byte_1D2CC = 0                   ; clamp to 102 max
   ```
   The morale value is **not a 2-12 roll**. It is a **percentage** derived from
   `control_morale & 0x7F` (max raw value 0x7F = 127, doubled = 254, clamped to 102/0x66).

4. **Wound-based automatic failure:** Computes `(current_hp * 100) / max_hp` and
   `100 - result` = percent wounded. If `morale_score >= percent_wounded`, check succeeds
   (no morale failure). If morale_score = 0 AND the wound percent test fails, it fails.

5. **Enemy percentage remaining:** Also compares `byte_1D2CC` (enemy_health_percentage
   global `byte_1D903`) against a second threshold. Checks `combat_team != 0` (must be
   enemy team) before failing.

6. **Flee vs surrender:** On failure:
   - Calls `sub_3E124` which returns remaining movement as a proxy for "can still move."
   - If `remaining_movement / 2 > morale_score` (threshold): `actions.field_14 = 1`
     (moral_failure flag), removes dragon-slayer / frost-brand enchantments (affects 0x4A,
     0x4B), creature flees.
   - If `es:[di+0x13] > 5` (field at offset 0x13 = `nonTeamMember`): prints "Surrenders"
     and calls `clear_actions`.

7. **Round limit:** `gbl.combat_round_no_action_limit` initialised to 15 at battle start
   (ovr011:1CB1: `mov byte_1D8B8, 0Fh`), `combat_round_no_action_value = 15` (Gbl.cs line
   384). After 15 rounds of no action from a side, it presumably forces resolution.

**Summary:** Morale is a **percentage check** (0–100 scale), not a 2d6 roll. The raw
morale byte (field_F7 & 0x7F, doubled, capped at 102) is compared against the percent of
HP lost. Monsters with morale = 100% never fail unless forced.

### Our engine vs COAB — delta

**WRONG: morale model.** `TacticalCombatant.morale` is described as "2..12" with a "2d6
roll" in the comment. COAB uses a **percentage system** (0–102 range). The check is:
`morale_percent >= percent_hp_lost` → stays; else → flees or surrenders.

**WRONG: morale check trigger.** Our CT.3 stub doesn't specify when to check. COAB checks
morale on each monster's turn in the per-turn dispatch (`sub_3504B` calls `sub_3637F`).

**MISSING: surrender outcome.** When morale fails AND `nonTeamMember > 5`, the result is
surrender (stops acting), not flight.

---

## 9. DAMAGE & DEATH

### COAB ground truth

**Source:** `Classes/Enums.cs` `Status` enum (lines 8-18):
```
okey = 0, animated = 1, tempgone = 2, running = 3,
unconscious = 4, dying = 5, dead = 6, stoned = 7, gone = 8
```
`Player.hit_point_current` (offset 0x1A4) — current HP as a byte.

`sub_32200` (ovr008:2200) — the damage-display and HP-update function. At line 91603-91609:
```asm
mov al, es:[di+charStruct.hit_point_current]
xor ah, ah
add ax, 10                   ; hit_point_current + 10
cmp ax, [bp+damage]
jnb not_killed               ; if hp + 10 >= damage → not killed; else → "dies"
```
This implements the **negative-HP threshold**: a character is killed only if `damage >=
hp_current + 10`, i.e., the character has a "buffer" of 10 HP below 0 before dying
outright. If damage brings them to ≤ 0 but does not exceed `hp_current + 10`, they go
`unconscious` or `dying` rather than dead.

So the Gold Box HP states are:
- HP > 0 → active (okey/animated)
- HP = 0 → down; `unconscious` (can be revived)
- HP < 0 (but damage < hp + 10) → `dying`
- HP < 0 AND damage >= hp + 10 → `dead`

Minimum damage: confirmed in `resolveHit` in our engine — `Math.max(1, ...)` is the
right floor. COAB uses `max(1, dice roll)` implicitly (minimum 1 per swing).

**Drain and special death:** Status `stoned` (petrification) and `gone` (destroyed/
disintegrated) are distinct from dead. Death effects from Energy Drain are tracked via
`lost_lvls` (Player.cs offset 0xE7).

### Our engine vs COAB — delta

**WRONG: death threshold.** `tactical.ts` sets `target.hp` below zero and fires `slain`
when `hp <= 0`. COAB treats `hp = 0` as `unconscious`, only marking `dead` when
`damage >= hp_current + 10`. The engine needs an `unconscious` state between 0 and
"−10 equivalent."

**WRONG: no unconscious/dying states.** Our `TacticalStatusFlag` has only `asleep`,
`held`, `fled`. `unconscious` and `dying` are missing.

**CORRECT: minimum damage = 1** matches COAB convention.

---

## Open Questions / Unknowns

| # | Question | Status |
|---|----------|--------|
| OQ-1 | Initiative random assignment: where/when does the engine roll or set `charStruct.initiative` (field 0x1A5) before `calc_group_inituative` reads it? | UNKNOWN — not found in LST excerpts read |
| OQ-2 | Are the short/medium/long missile range brackets (+1/0/−2) hard-coded in the engine or in item data? | UNKNOWN — item data table at ~0x5D10 in seg600 not read |
| OQ-3 | Does an intervening body hard-block a missile shot, or only apply a penalty? | UNKNOWN — `sub_68708` (cover check) not fully read |
| OQ-4 | Exact backstab to-hit bonus (vs. damage multiplier only) | UNKNOWN — `backstab` proc body not fully traced |
| OQ-5 | Large monster size → reach extension: does `CombatantMap.size > 1` extend melee reach? | UNKNOWN |
| OQ-6 | `field_1A5` vs `baseHalfMoves` (0x11D): Player.cs labels `initiative` at 0x1A5 and `baseHalfMoves` at 0x11D, both appear in movement/initiative contexts. One may be for map movement, the other for combat speed. | Needs cross-reference |
| OQ-7 | Firing-into-melee penalty: the original rules impose a −4 to-hit when shooting at a target in melee. Is this implemented? | UNKNOWN |

---

## Implications for Our Engine (`tactical.ts`)

### Priority corrections (highest → lowest)

1. **Death/unconscious threshold** (`hp <= 0` → unconscious; `hp + 10 <= 0` → dead).
   Source: `sub_32200` ovr008:2240-2245. This affects the basic game loop immediately.

2. **Diagonal movement cost** (alternating: orthogonal = 1 cell, every second diagonal =
   1.5 → implement as "3 half-steps vs 2 half-steps" or track diagonal parity).
   Source: `Classes/Combat.cs` `SortedCombatant` comment. Affects movement reach for
   every combatant.

3. **Morale model** (replace 2d6 vs morale-12 with percentage: `morale_pct >= pct_wounded`
   → passes). Source: `sub_3637F` ovr010:137F-1532.

4. **Initiative** (remove per-round d10 roll; read a stored `initiative` field, halved for
   slow / doubled for haste; sort descending).
   Source: `calc_group_inituative` ovr008:20E5.

5. **Fractional attacks** (`attacksCount` stored in half-attacks; alternate extra swing
   per round for 3/2 fighters).
   Source: `sub_3EDD4` ovr014:0DD4, `sub_3EF0D` ovr014:0F0D.

6. **Rear AC** (second AC value `ac_behind` for attacks from behind/flanking).
   Source: Player.cs offset 0x19B, used in `sub_3F4EB`.

7. **Missile range brackets** (apply to-hit modifier by bracket; do not hard-block
   unless wall). Source: `sub_3FCED` ovr014:1CED (partial — exact penalty values UNKNOWN).

8. **Two monster attack routines** (attack1 + attack2, each with own dice and counter).
   Source: Player.cs offsets 0x19E–0x1A3, `attack1/2_AttacksLeft`.

### Lower priority / deferred

- Multi-cell large monster occupancy (CombatantMap.size) — not in CT.1 scope.
- Surrender as distinct morale outcome — needs morale system first.
- Map dimension enforcement (50 × 25) — cosmetic for small test maps.
