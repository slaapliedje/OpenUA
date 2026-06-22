# Dungeon 3D-view — stack audit + card deck (#129)

Built 2026-06-22 after the symptom-chasing dead-ended ("everything port-side
reads faithful, yet the render differs from the Mac"). The cause is NOT one bug
in the verified walk — it's a cluster of **stand-ins UNDER the walk**. Work them
as cards, faithful-lift each, smallest slice first.

## What's FAITHFUL (do NOT touch — full lifts, agent + trace verified)
The frustum walk and everything below it:
`jt199`/`l5b42`/`l5e52`/`jt200`/`jt200_layer`, the blit chain
`jt114`/`jt1001`/`jt995`/`l309c`/`l2d4e`/`jt1135`/`l2856`/`l37aa`/`jt1161`/
`jt1173`/`jt1193`/`jt993`/`jt124`, the wall loaders
`cw_wallfile_load`/`l6eea`/`l6148`/`wallset_for_id`, the entry `jt221`/`jt935`,
the region-fill primitives `jt116`/`jt118`/`jt121`, AND the faithful GLIB
shared-palette allocator `l6e58`/`jt1069`/`jt1066`/`jt1068`/`l3f3c` —
**but the palette allocator is currently never reached for the 3D walls** (see
card 1). Map LOAD is byte-perfect (docs/dungeon-view-wall.md 06-22b).

## The stand-ins / stubs / missing (THE CARD DECK, ranked)

### Card 1 — CLUT: shared palette — STARTED 2026-06-22 (grounding experiment)
Un-gated `l58c4` (`g_dungeon_bigpic_overlay=1`) and captured the standing frame.
TWO findings:
1. The HUD ROSTER PANEL went grey/blank — confirming the entanglement is the
   dungeon's 32..255 shared palette vs the HUD **text** colours (also in
   32..255, the port_hud_text_clut band), NOT the menu's 0..31 (l3f3c only
   writes 32..255). So the real Card-1 work is: make the dungeon shared palette
   and the HUD text colours coexist (the Mac repaints the HUD after, or uses
   distinct ranges) — a HUD-palette untangle, not just a gate flip.
2. **The 3D view's door/wood was UNCHANGED** by installing the faithful palette.
   So Card 1 is NOT the visible wall bug — the door-as-wood-at-edge is a
   PLACEMENT/PIECE thing, not colour (matches the user's "it's not colour").
   Combined with: walk faithful, map byte-perfect, layout faithful (card 2),
   see-through faithful — the door/wood may even BE faithful (the cell is
   genuinely group 1, the placement is the verified layout), and
   data/mac_3d_start_e.png may be a different frame than the true 10,8,E
   standing view. The ONLY way to settle faithful-vs-bug now is the Mac's exact
   10,8,E standing frame (trace or fresh screenshot at a confirmed position).
Reverted the gate (experiment only). Card 1 remains a real faithfulness lift
(the fabricated band CLUT is still a stand-in) but is DE-PRIORITISED for fixing
the visible bug.

### Card 1 (detail) — retire the fabricated band model, run the faithful shared palette
- `cw_finalize` (boot.c:9696) — **PORT STAND-IN**: builds a fabricated 256-entry
  CLUT from per-set 32-bands (`g_cw_base`, `g_cw_remap`, hardcoded ceiling/floor
  pal[4]/pal[5]). NOT the Mac's single shared dungeon palette.
- `load_wall_groups` CLUT install (boot.c:9756), `cw_load_slot` (boot.c:9615) —
  PORT STAND-INS feeding cw_finalize.
- `l58c4` (boot.c:2762) — the FAITHFUL backdrop+shared-palette path — is GATED
  OFF (`g_dungeon_bigpic_overlay = 0`, boot.c:2752) *because* it installs the
  real palette into CLUT 32..255 and clobbers the fabricated bands. So the
  faithful `l3f3c`/`jt1069`/`jt1066` palette path is DEAD for the 3D view.
- Lift: make the dungeon use the faithful shared dungeon palette (`l3f3c` /
  the GLIB allocator), retire cw_finalize's bands, un-gate `l58c4`. This is the
  wood-vs-stone / wrong-colour root.

### Card 2 — Layout globals — DONE 2026-06-22: it was already FAITHFUL
RESOLVED: there is NO faithful "writer" — the disasm has zero `movew` to
-12240..-12202; the globals are DATA-segment initialized. Dumped what
data_pool_replay loads pre-override: `5,4,6,4,2,7,2,0,9,5,4,3,3,3,1,1,1,0,0,4,0,0`
— byte-identical to the hardcoded override. So the layout was loaded faithfully
all along; the override was a REDUNDANT stand-in (and the "DATA held off-screen
175/516" claim was wrong). Retired the override (boot.c:8728). **Layout is ruled
OUT as the 3D bug — the door's edge `top` is the faithful placement.** This
narrows the bug to Card 1 (CLUT) / Card 3 (orchestrator) / Card 4 (3-pass).

### Card 2 (original) — Layout globals: lift the faithful writer (the MISSING function)
- `-12240..-12198` (22 words) seeded at **boot.c:8728** from a HARDCODED
  `static const short layout[22]` pasted from a live mon capture. The faithful
  launch-time writer that the Mac runs to populate these is **NOT LIFTED**
  (the DATA image holds different/off-screen values; "a launch-time init
  overwrites them" — writer UNLOCATED, docs/dungeon-view-wall 14g).
- `jt199` reads all 22 every frame → these ARE the slot screen-deltas → the
  door's `top=8004` (edge) placement comes from here. The zeros in the table
  (idx 7/16/17/18/20/21 = 0) are the suspats.
- Lift: locate the Mac writer (search the disasm for writes to A5 -12240..),
  lift it, replace the pasted table. **This is the slot-placement root.**

### Card 3 — render_3d_faithful: replace the stand-in orchestrator
- `render_3d_faithful` (boot.c:11347) — **PORT STAND-IN** glue: hardcoded
  `g_cwf_ox=20/g_cwf_oy=44` ("re-tune by screenshot"), forced `g_cwf_force_deep
  =1`, inline region-fill reimplementation, the faithful `l57f2` shell relocated
  + gated. Replace with the faithful `jt221` dungeon arm (L57f2 → L6234 → tail)
  once cards 1/2 make the faithful palette + placement live.
- `l57f2` (boot.c:2812) — SKELETON (reduced to gated `l58c4()`).

### Card 4 — the absent 3-pass floor/ceiling/feature draws
- `l2282` / `jt106` / `l20cc` — **DO NOT EXIST**. The Mac draws floor/ceiling/
  feature tiles in a 3-pass loop around `jt199`; the port only has the inline
  region fills + the stand-in backdrop blit. Lift the 3-pass.
- `load_backdrop` (boot.c:9777) + the inline `g_back_img` blit
  (boot.c:11458-11511) — PORT STAND-INS (BACK.CTL → map_px fill).

## Order of attack
Card 2 (layout writer) and Card 1 (shared palette) are the two roots — placement
and colour. Card 2 is the cleaner first slice (a single missing writer; the
door's edge `top` is the direct symptom). Then Card 1 (palette). Cards 3/4 fall
out once the faithful palette + placement are live.
