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

### Card 1 — SOLVED 2026-06-22: the lost per-group palette REBASE (wood-vs-stone root)
The "palette is an entanglement, not the bug" conclusion below was WRONG — it was
reached without inspecting the actual wall art. Reading 8X8DB.CTL directly settled it:

- Every wall set's item-0 palette declares **start=32, count=37** (`hdr[1]&1`,
  `hdr[2..3]=32`). Tile bytes are DIRECT CLUT indices into a base-32 palette
  (solid-wall pieces use bytes 34..61 — BEYOND the 37-entry palette, confirming
  they index the installed CLUT, not the raw palette array).
- HEIRS Wall1=set5 (STONE: pal is a grey ramp at 14..29), Wall2=set8 (WOOD: brown
  ramp at 2..12), Wall3=set1 (=set5, stone). All three want CLUT 32..68 yet differ
  over the SAME byte range — a flat palette physically cannot render both.
- The Mac resolves this with the GLIB colour-range allocator (jt1069): stone sets
  (ncopy=0) keep 32..68; wood sets (set8/set2 carry **ncopy=1** remap entries) are
  RELOCATED to a free band and their pixels remapped. cw_finalize REPRODUCES that
  relocation by banding the three slots at 32/64/96 (`g_cw_base`) — it is NOT a
  fabrication. The missing half was the per-group pixel **rebase at blit time**.
- BUG: `jt114` routed the wall blit through `l309c` (DIRECT, no rebase) instead of
  the rebasing `l309c_tile`. So Wall2's wood tiles (bytes 34..41) hit CLUT 34..41 =
  Wall1's band -> wood walls read Wall1's palette (an incoherent stone/brown mix).
  Invisible at the start cell (mostly Wall1 = slot 0, base 32, where the rebase is
  the identity) — it only shows when you move and Wall2/Wall3 faces appear. Matches
  the user's "once you move east the view becomes whole".
- FIX: route `jt114` -> `l309c_tile` (rebase `pixel = g_cw_base[slot] + (byte-32)`,
  keyed by `g_cwf_slot` = the wall group). Start frame unchanged (slot-0 identity).
  PROVEN on the real HEIRS bytes: OLD byte41->black, byte40->bright-brown (set5's
  band, incoherent); NEW byte41->(143,115,59), byte40->(127,99,39) = set8's own
  coherent WOOD ramp. (host sim in the commit log; live Hatari visual pending —
  the load->dungeon path is blocked by the harness's relative-mouse drift.)

### Card 1 (OLD, SUPERSEDED) — CLUT: shared palette — grounding experiment 2026-06-22
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
