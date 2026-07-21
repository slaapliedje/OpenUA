# Play-vs-Edit mode audit

2026-07-21. Prompted by a user report that dead-on matched the git history:
wall collision worked in the first playable build, then silently died when the
editor code landed. This document is the audit of every mode discriminator and
every function shared between the play and editor paths, so the split stays
explicit. Update it when a shared function's gating changes.

## How the walls died (the history the user remembered)

- `e64e9ed` / `0dcb18bd` (May 31): the first playable walk was port-side
  `party_step()` + `party_passable()` — walls blocked (using the edge's
  movement low nibble == 0, which is NOT the real rule, but close enough on
  the tutorial maps).
- `9abef02` (Jun 5): the faithful CODE 22 editor walk driver (`jt297` +
  `l1908` + `l05ca`) was lifted and wired into play, replacing the demo walk.
  Its only forward check is `fp4 && l05ca(cell,dir) > 1`, where
  `fp4 = (rec[4]==0 && rec[9]!=0)` — a **wilderness-record** check. The play
  loop builds a dungeon record (`play_rec[4] = 1`), so from this commit on the
  play walk had **no wall check at all**; `jt218`'s (faithful) toroidal wrap
  then teleported the party whenever a wall-pass crossed the map edge.
- `3b340d6` (Jul 21): fixed — `play_can_pass()` lifts the passability core of
  the real Mac play mover `JT[955]` (CODE 21+0x453c); `jt297`'s play steps
  gate through it.

`party_step`/`party_passable` still exist but only drive the boot-time demo
sequence; their low-nibble rule is wrong (it would block edges the real game
opens). Do not promote them; they are deletion candidates once the demo
sequence retires.

## The two driver stacks

| | Mac play | Mac editor (what the port reuses for play) |
|---|---|---|
| mode loop | `jt948`/`jt953` (CODE 20/21) command loop | `l0096` (CODE 22) editor mode loop |
| walk loop | jt953 command switch; move = `JT[955]` → commit `JT[954]`=L38f4 | `jt240` (CODE 11+0x4ffe) → `l63c0` → `jt297` → `l1908` |
| wall rule | JT[955]: switch on wall-ART ahead (JT[210] hi nibble): 0 open, 1 secret door (pass, "A secret door!"), 6..13 pass iff game-rec[63+code] (the 8 SET-FLAG-togglable walls, rec[69..76]), 14 solid, 2/3/4/5/15 "Blocked:" force/knock/pick menus | forward: `fp4` wilderness l05ca check only; dungeon records fly-through |
| step commit | JT[954]: `jt52(11)` step sound → delta move → **toroidal wrap** → cache JT[202]/JT[201] → `jt914(1, search?2:1)` clock | `l1908`: `jt218` move (same wrap), no sound, no clock |

On the Mac, `JT[240]` has exactly ONE caller — CODE 22+0x334, the editor. The
Mac play NEVER runs the l63c0 stack. The port runs play through it anyway
(ADR-era decision; the jt953 command dispatch is grafted on top), so every
play behaviour the editor driver lacks must be added under a play gate:
today that is `play_can_pass` + the JT[954] tail (sound, clock, jt938 repaint)
in jt297's GAP-1 block.

## Mode discriminators — the authoritative list

| flag | owner | meaning | use for |
|---|---|---|---|
| `g_geo_editor_active` | PORT | 1 across the whole `l0096` editor mode loop (and the editor render scope in `jt56`'s art path) | **the** play-vs-editor test for every port-added gate |
| `g_a5_-18485` | Mac | multi-state mode/pending byte. Observed writes: 0 (default; port play scaffold), 1/2 (CODE 11+0x3348, the walk-loop mode-dispatcher case 7 = editor walk-test entry, +1 if `jt318()`), 5 (CODE 9+0x30d4 — full design editor). Faithful reads: `==0` fresh-vs-resume (l0bbc, L07dc), `!=0` "design arm" (CODE 20+0x57ac reload gate, CODE 21+0x3ba6/0x3c16 spell-list, l143e family), `==5` design-allows-everything (CODE 16 l4e2c — the ONLY `cmpib #5` in the binary) | faithful lifts only. **Never** use it in port-added play/editor gates. All its faithful writers are lifted and live: l30d4 sets/restores 5, jt243 posts (case 7) and consumes+clears (case 1) the 1/2 values |
| `g_a5_-27990` | Mac | play-screen state (3 = overland render, 4 = dungeon view, 0 = none; jt955 switches its move arms on it) | faithful |
| `g_a5_-18878` | Mac | current level number; <= 4 overland/city legs, >= 5 dungeon (the play loop's leg split) | faithful |
| `rec[4]` (walk record) | Mac | **area kind**: 0 = wilderness, 1 = dungeon (NOT an editor flag). `rec[5]` cell kind, `rec[9]` = `(jt318()==0)` | faithful; play builds `play_rec[4]=1` |
| `jt1160()` / `-2592` bit 1 | Mac | view mode: top-down/automap vs first-person | faithful |
| `g_walk_cmd` | PORT | command-bar pick latched out of l63c0 for the play dispatch | play loop only |

## Shared-function verdicts

| function | shared how | gating | verdict |
|---|---|---|---|
| `jt240` / `l63c0` | editor loop, reused as play walk | play grafts: command bar, HUD, g_walk_cmd | OK (by design) |
| `jt297` | movement dispatch | play steps gate through `play_can_pass` via `snapped && !g_geo_editor_active`; editor keeps fly-through | FIXED (was `g_a5_18485 != 5`, dead-true) |
| `jt297` GAP-1 | per-step events + JT[954] tail | whole landed-on-a-new-cell block gated `!g_geo_editor_active` (events, sound, clock are play machinery) | FIXED — editor walks fire nothing (verified: no STEP/CELLSCAN entries from editor movement) |
| `l1908` | the step body | `jt299` map recompose gated `moved && g_geo_editor_active` | FIXED (was `== 5`, dead-false: editor map leg never recomposed) |
| `jt299`→`jt303` (editor status header) | leaks over play HUD | `g_geo_editor_active` in the l63c0 initial paint | FIXED (was `== 5`) |
| `l5126` (deep status header) | same | `g_geo_editor_active` | FIXED (was `== 5`) |
| `l2cf4` (editor cursor/selection box) | drawn from shared `jt935` | `g_geo_editor_active` | FIXED (was `== 5`) |
| `jt312` + HUD block (l2c60/jt937/jt938) | editor 3D preview reuses jt312 | `!g_geo_editor_active` skips the whole play-HUD block | OK (already correct) |
| `l05ca` | wall-art nibble reader | check itself is mode-free; the `fp4` caller is the wilderness rule | OK |
| `play_can_pass` | play passability (JT[955] core) | play only via callers | OK — menus (art 2/3/4/5/15) and "A secret door!" deferred |
| `jt311` | overland/top-down mover (play Area + wilderness) | play gate added: keys 257..264 are dirs 1..8 (`dir = key - 256`); overland (`-27990==3`) blocks on any wall art (JT[955] case-3 L4816 core), dungeon automap (`==4`) uses `play_can_pass`; editor keeps free movement | FIXED (core) — L4816's zone announcements / terrain-time tail still deferred |
| `l40f8_area_cmd` | play Area toggle | play-only caller | OK |
| Search/Look/Cast/View/Inv arms | jt953 grafts in the play loop | operate on the play game record | OK |
| `party_step`/`party_passable` | boot demo only | — | deletion candidates; wrong rule, do not reuse |
| `l4e2c` spell predicate | faithful `-18485 == 5` | faithful | OK — do not "fix" to g_geo_editor_active; it is the Mac's own test |

## Rules going forward

1. Port-added play-vs-editor gates use `g_geo_editor_active`, nothing else.
2. `g_a5_-18485` is Mac state: lift it faithfully, write it where the Mac
   writes it, and never borrow it for port gating. Its faithful writers are
   all lifted and live (see the CODE 9 census below) — no port writes are
   needed anywhere.
3. Anything grafted onto the shared l63c0 driver for play must be gated
   `!g_geo_editor_active`; anything editor-only must be gated
   `g_geo_editor_active`.

## CODE 9 census (2026-07-21)

All five JT exports are lifted and live: jt322/jt323/jt324 (record-editor
family), jt325 + jt325_tail (the record editor), jt326 = l0004 (the menu-bar
dispatcher). The l30d4 spell-memorization sub-editor is a full lift INCLUDING
the faithful -18485 = 5 write with save/restore, and jt243's fill/walk arms
post (case 7 @0x3348) and consume+clear (case 1 @0x12fc) the 1/2 values. So
-18485 is FULLY faithful in the lift; no port writes are needed anywhere (an
interim session-wide 5 in l0004_22 was added and then removed the same day —
it would have leaked design-mode spell rules into l0096's play-test mode 14).

## Verified after the fixes (2026-07-21, Hatari)

- Play (Save B): wall east of 6,17 blocks, clock ticks on landed steps only.
- Play (generated 24x24 WALLTST module): forward into an art-14 N-edge wall
  refuses (no STEP log, no time); turn W + forward passes (STEP, +1 min) —
  play_can_pass holds on arbitrary design data, not just HEIRS.
- Play entry: the stray yellow box at the 3D frame's top-left is gone — it
  was jt304's editor marked-cell icon (jt213 on rec[46..48]) drawn once by
  l63c0's entry compose with unconfigured map anchors; now editor-gated.
- Editor (Edit Modules): picker + canvas healthy, cursor mark draws, arrows
  fire no events/clock (zero CELLSCAN entries).
- Dungeon AREA view keeps FIRST-PERSON movement (live-verified: Up stepped
  forward, Right turned) — jt1160 stays false there, so the jt311 play gate
  is currently exercised only by the editor map mode and the future overland
  walk; overland play movement itself still routes jt953 -> jt955, and jt955
  is a PROBE stub (its lift inherits the faithful rule; the jt311 gate stays
  as defence-in-depth).

## Known follow-ups

- Editor cursor leaves a mark TRAIL: the map view doesn't repaint between
  cursor moves, so revived l2cf4 marks accumulate. Cosmetic; needs the
  editor's per-move repaint (jt299 path) wired for the PLACE-mode cursor.
- JT[955] overland arm (L4816): zone-crossing announcements + terrain time
  costs; overland steps also advance no clock yet (JT[954] tail is
  first-person-only via GAP-1).
- jt955's "Blocked:" menus (art 2/3/4/5/15) + "A secret door!" (L4526).
- jt955 (CODE 21+0x453c) full lift — the overland/dungeon forward-move
  handler; its dungeon core lives on as play_can_pass, the menus and the
  overland arm (L4816) are the missing pieces. jt954 (the step-commit) is
  already fully lifted at its l2e42 site.
