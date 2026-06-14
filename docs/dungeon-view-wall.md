# Dungeon 3D-view worklist — the wall-tile geometry gap

## UPDATE 2026-06-14b — FULL asm read of the blit chain: render pipeline is FAITHFUL
Read the entire chain in the disasm (CODE_05/07) and cross-checked each stage
against the port. EVERY primitive is a faithful match — the render CODE is not
the bug. Ruled out, with asm cites:
- **Cell read `l5e52`** (CODE_07 0x5e52): the real code reads the edge byte at
  ds+290+(col*H+row)*6+(dir&6)/2 and returns `& 15` = the LOW nibble. The port's
  l5e52 returns `*cell & 15` — MATCH. (jt210/l5bfa reads the HIGH nibble for a
  DIFFERENT purpose; the frustum uses the low nibble. Not a high/low-nibble bug.)
- **Walk `jt199`/L6234**: map-verified faithful (prior), emits ~25 slots post-#128
  (matches the Mac count).
- **Axis `l5b42`**: top<-wide(-12222, soff-varying), left<-narrow(-12202); trace-
  confirmed (`top` steps 8032->8012, `left` constant). MATCH.
- **`jt200`/L59d4**: code fold-by-5 -> (group,style); far/near idx math
  (near = code*9+sub+2 / code*10+sub+1) MATCHES L59d4 5aba/5ad4. The per-layer
  LEFT step is `+4` NATIVE (the Mac's deep `+16` is the 640x480 2x display path,
  which our native port correctly does not use). MATCH.
- **Blit `L309c`->`L2d4e`->`L2970`**: (a) TRANSPARENCY = byte 255 (the L2970
  masked path pushes #255); port honors `v==255`. MATCH. (b) BEARINGS: L309c does
  `coord -= metric[2:3]` / `metric[4:5]`; jt1005 (faithful L31fc, L309c's bbox
  sibling) proves metric[2:3]=ascent(vertical), metric[4:5]=x_bearing(horizontal);
  the port subtracts ybear=metric[2:3] from the vertical axis and xbear=metric[4:5]
  from horizontal. MATCH (a swap hypothesis here was tested vs the asm and is
  WRONG — the stack-order vs physical-axis labeling is the confusion). (c) MODE:
  wall flags 0xc5 -> low-nibble 5 = raw 8bpp -> L2970 (not composite/PackBits/RLE).
  Single-body blit MATCH.
- **Positions are NATIVE**: l5b42 applies `delta<<2` (delta*4) ALWAYS (asm 5b4c,
  not gated on scale); the deep remap (the extra <<2) is the 2x display path the
  port omits. So port positions = native delta*4 + hole origin. The tiles are
  authored at NATIVE (VGA 320x200) size per the user (Mac port is from DOS/VGA;
  the Mac 2x is just its 640x480 display feature; our Atari is native-fullscreen
  like DOS), so tiles 1:1 is correct too.

### So the bug is NOT in the render code — it is EMPIRICAL (data or frame)
With the whole pipeline faithful yet the render = flat stone wall (left ~60%) +
sky (right) instead of the Mac's symmetric corridor+door, the divergence must be:
  (1) the loaded GEO005 cells the LATERAL scan reads (col7/col9 neighbours of the
      10,8 origin) differ from the Mac's, so the frustum assembles an asymmetric
      view (the render IS faithful to whatever cells it reads); OR
  (2) the loaded 8X8DB tile PIECES (via the -27894 handles / l6eea) differ from
      the Mac's for the emitted idx; OR
  (3) the reference screenshot is a different frame/state than what loads.
DECISIVE NEXT STEP (host-side, no live capture needed if done offline): diff the
port's emitted (code,sub,top,left) slots against the Mac standing trace
(docs/mac-blit-trace-heirs-l5-standing.md, 25 slots). MATCH -> bug is tile DATA
(2); MISMATCH -> bug is the cells read (1), i.e. the loaded map still differs at
the scan neighbours. Get the port slots via an offline jt199 replay on the dumped
map (/tmp mapdump: party 10,8, deltas tbl-27853/-27862, slot-layout -12240..),
since the live capture is blocked by the looping entry-event script (a #100
matter, NOT skipped by FRUA_SKIP_ENTRY_EVENTS which only skips l709e(special)).

## UPDATE 2026-06-14 — colour is FIXED; the gap is now PURELY geometry
Captured the committed-baseline `render_3d_faithful` output (VIEWPORT.PPM at the
standing 10,8,E frame, /tmp/prefix_5x.png):
- **Colour is RESOLVED.** The 88x88 render is clean greyscale stone — 22 grey
  shades, ZERO menu-colour bleed. The "texture/CLUT-band scramble" this doc
  chased (the l309c_tile 32-band rebase passing idx 0-31 into the UI band) is no
  longer the active bug; c89d5f2 (g_clut_clobbered reinstall) + #128 (GEO load)
  fixed it. Do not re-open the CLUT-band line first.
- **Remaining gap = wall-tile GEOMETRY.** The port renders a FLAT full-brick
  column shoved to the LEFT ~60% of the pane, sky opening on the right, wood
  floor below. The Mac (data/mac_3d_start_e.png) is a SYMMETRIC angled corridor:
  side walls converging from both edges to a centre vanishing point, far wooden
  door centred, night sky top-centre. The corridor angle comes from the
  pre-mirrored WEDGE tiles (idx 6 left / idx 7 right); the port is laying
  full-wall tiles (idx 1/2) instead, with the bands receding leftward (`left`
  8028->8024->8016 -> screen-left) rather than mirroring L/R.
- **The X-mirror / l5b42-axis hypothesis is DISPROVEN** (see memory
  [[dungeon-frustum-xplacement]] + commit 2d4604b): l5b42's axis (top<-wide
  -12222 stepping by soff, left<-narrow -12202, step on left) is faithful,
  triple-verified (asm L641a/L65b2 + the Mac blit trace `left constant, top
  steps` + a live swap that produced a flat wall = worse). Do NOT touch
  jt199/l5b42 coords.

### THE decisive open question (gates whether the fix is jt199 vs l309c_tile)
The 2026-06-13 "byte-identical Mac trace" claim (below) PREDATES #128, which
changed which GEO cells/codes are read -> the port's emitted (top,left,code,sub)
slots may have changed. So: **do the port's CURRENT post-#128 slots still match
the Mac at the confirmed 10,8,E standing frame?**
  - If YES -> slots faithful, bug is in l309c_tile (wedge-tile SELECTION/SIZE/
    bearing; the port lays full-wall tiles where the Mac lays wedges).
  - If NO  -> jt199/l5b42 read the wrong cells/codes post-#128; re-derive the
    walk against the SAME-FRAME trace.
Get this via a fresh J200DIFF.TXT (FRUA_VIEWPORT_DUMP build) AND the user's
instrumented-Mac same-frame jt200 trace at mon-confirmed row10/col8/facingE
(the capture spec is below). The user THIS session confirmed mac_3d_start_e.png
IS the 10,8,E standing frame (map-verified: r0ac08 all-open, r0bc08 door-E), so
that screenshot is a valid oracle even without a fresh trace.

### Harness note (2026-06-14)
The live ENTRY viewport is NOT render_3d_faithful — its VIEWPORT.PPM dump never
fires on first paint, yet pixels are on screen, so a placeholder/other renderer
draws the entry frame; render_3d_faithful only runs on a redraw. Also the scripted
caravan/merchant ARRIVAL events are NOT skipped by FRUA_SKIP_ENTRY_EVENTS — press
Return ~25-30x through them to reach the HUD, THEN an arrow/Return triggers the
faithful redraw + the dump.

---

Status as of 2026-06-13 (superseded above re: colour). The first-person view
(jt199 frustum -> l5b42 -> jt200 -> l309c_tile) rendered a *broken mirror*: every
wall tile lands in one half of the 88x88 pane, the other half empty, the wooden
door split. Target = `data/mac_3d_start_e.png` (a clean symmetric stone corridor,
centred door, sky above), at party row=10 col=8 facing=2/E on HEIRS save A.

## What is RULED OUT (do not re-investigate)

- **Tile library** — `data/work/gamedata/8X8DB.CTL` is BYTE-IDENTICAL to the Mac
  original (`data/frua-mac/joined/Disk4/8X8DB.CTL`, 296414 B). make-gamedata ships
  it verbatim.
- **Tile decode** — wall tiles are `flags=0xc5` => mode 5 = raw 8bpp. `l309c_tile`
  reads `body[r*w+c]` at stride `w = bpp_w*8`, 0xff = transparent. Bodies are clean
  (flat fields + tidy perspective wedges). Verified against the live file.
- **Per-side flip** — tiles 6 and 7 are a PRE-MIRRORED pair (6 = left-opaque wedge,
  7 = right-opaque wedge). The Mac *selects* per side; it never flips at runtime.
  The port's trace uses both (idx6 #17/#19, idx7 #21/#22), so selection is present.
- **The blit itself** — an offline render of the EXACT recorded J200DIFF slots
  (real tile bytes + recorded tx/ty + bearings) reproduces the same broken image
  as the live port. So the port faithfully blits the slots it emits.
- **Palette / CLUT band rebase** — structurally faithful; a colour issue would
  read as wrong colours, not a placement scramble. (User confirmed: not a colour
  scramble, a *texture/placement* scramble.)

## ROOT CAUSE — the view-layout delta table has no left/right x separation

The per-slot screen deltas live in `g_a5_-12240..-12198` (22 words), seeded by a
hardcoded `layout[22]` mon snapshot at `src/engine/boot.c` ~8008. The x-deltas each
pass reads (l5b42 does `left = x + xdelta<<2`, NO per-side sign flip):

| pass            | left x-global | right x-global | values   |
|-----------------|---------------|----------------|----------|
| side near-front | -12220        | -12220         | 4 , 4    |
| side recede     | -12202        | -12202         | 4 , 4    |
| front           | -12218        | -12216         | **3 , 3**|
| band gxB        | -12212        | -12210         | **1 , 1**|

Front and band give left == right, so both walls land at the same x => everything
collapses to one side. A symmetric corridor needs the left passes at a smaller /
negative x and the right at a larger x.

The pass STRUCTURE is faithful to the asm — verified `CODE_07.s` L641a (left side
loop) and L65b2 (right side loop) BOTH read `-12220` and `-12202`, matching the
port. So jt199's logic is fine; only the TABLE VALUES are wrong (the mon snapshot
that overrode the DATA-image values `175/516/...` is incorrect/incomplete).

## NEXT — re-derive the table (task #129; #126 reopened)

1. Find the Mac's view-layout INIT that computes `-12240..-12198` (the "launch-time
   init" the boot.c comment cites). Grep the disasm for an indexed/loop store to
   the a5 window (a direct `movew Dn,%a5@(-122xx)` search came up empty — it writes
   via a pointer/`lea` + loop or BlockMove). Lift it so the table is computed, not
   snapshotted.
2. If no clean init exists, re-capture `-12240..-12198` from the real Mac via the
   BasiliskII mon (CurrentA5 @0x0904; the standing frame), AND determine how L/R
   separation is encoded — either the real values are asymmetric, or l5b42 must
   negate `xdelta` for one side. The DATA-image values `175/516/...` may BE the
   correct large deltas with l5b42's transform doing the work; check that path
   before trusting any small-delta snapshot.
3. VALIDATE against `data/mac_3d_start_e.png` (symmetric corridor) AND an offline
   render of the recorded slots — NEVER against the right-heavy 25-slot trace
   (`docs/mac-blit-trace-heirs-l5-standing.md`). Code-multiset match is NOT
   position match (that error closed #126 prematurely).

## UPDATE 2026-06-13 — table re-derivation is MOOT; need a same-frame Mac trace

The view-layout table was NOT the bug. The DATA image (g_a5_init_bytes, loaded by
data_pool_replay) already holds 5,4,6,4,2,7,2,0,9,5,4,3,3,3,1,1,1,0,0,4,0,0 at
-12240..-12198 — IDENTICAL to the "mon snapshot" hardcoded at boot.c ~8008. Those
ARE the faithful Mac static values (the table is written by no CODE except the
runtime long-store to -12200 from -21148 in CODE 11). The override is redundant;
the "175/516 off-screen DATA" comment was simply wrong.

Decisive comparison (tools: parse J200DIFF.TXT vs docs/mac-blit-trace-heirs-l5-
standing.md): the port emits (code,sub,top,left) BYTE-IDENTICAL to the 25-slot
trace — all 19 unique slots match the Mac positions EXACTLY at scale 2. Plus the
decoded runtime map (party 10,8,E) shows the IMMEDIATE view is asymmetric:
(10,9) near-left N=0 (open) / near-right S=11 (wall); only (10,10) is symmetric.
So the right-heavy render may be geometrically CORRECT for this cell, and the
symmetric `data/mac_3d_start_e.png` may be a DIFFERENT (post-move) frame.

=> Two Mac references disagree (symmetric screenshot vs right-heavy trace) and the
port matches the trace. Cannot resolve host-side. NEED: the Mac's actual jt200
blit slots captured at the EXACT same frame as the screenshot, mon-confirmed
party=10,8,2, BEFORE any movement.

### Ground-truth capture spec (for the user's instrumented Mac)
Same format as the existing 25-slot trace, but with same-frame proof:
1. Boot HEIRS save A, reach the standing dungeon view. DO NOT MOVE.
2. Shoot the screenshot AND capture the trace in the SAME session/frame.
3. mon: CurrentA5 @0x0904 -> read words A5-12288 / A5-12287 / A5-12286; CONFIRM
   = 0x000A / 0x0008 / 0x0002 (row10,col8,facingE). Record them with the trace.
4. Log every jt200 (= JT[200] / L59d4) call in order: its 4 stack args
   top, left, code, sub (the same fields the old trace lists). ~25 calls.
5. (Optional but ideal) also log each JT[114] blit's idx + (v,h) so far/near idx
   can be checked.
Then diff against the port J200DIFF.TXT (same fields). If positions differ, the
bug is l5b42/anchor; if they MATCH, the port is faithful and the screenshot was a
different frame (close #129).

## RESOLVED DIRECTION 2026-06-13 — walk is FAITHFUL; bug is the CLUT model

The user captured the Mac's live jt200 trace at the standing frame: it is
BYTE-IDENTICAL to the port's J200DIFF (same 25 slots, same code/sub/idx, same
JT114/JT999 blit coords, repeating each frame). So the frustum walk + slot
emission + group fold + idx are 100% FAITHFUL. #126 is genuinely done; stop
touching jt199/l5b42 geometry.

The "broken mirror / texture scramble" is the wall TILE COLOUR path:
- Wall tiles are DIRECT indices into a shared ~64-colour dungeon CLUT (set5 tile8
  uses idx 41-60, set8 0-53, set1 0-61 — overlapping, spanning BELOW 32). Rendered
  each with its own palette they're correct: set5=grey brick, set8=wood, set1=grey
  cobble (/tmp/tile8_sets.png).
- The port's per-set band rebase (l309c_tile: off=v-32; if 0<=off<32 -> base+off
  else pass through; bands 32/64/96) only remaps idx>=32 and PASSES 0-31 THROUGH
  UNCHANGED. But 0-31 is seeded as the UI/chrome band (cw_seed_ui_band). So set8/
  set1 tiles (heavy users of idx 0-31) paint big regions in MENU colours = the
  scramble. set5 (stone) only uses 41-60 so it survives -> stone looks ~ok, wood/
  cobble garble. CW_BAND=32 is too small; the tiles want a ~64-entry palette.
- The fix is a CLUT-model change, NOT geometry: treat wall-tile bytes as direct
  indices into the dungeon CLUT the Mac loads (GLIB palette subsystem / per-level
  clut), instead of fabricating 32-wide per-set bands from each sub-GLIB's tile-0.
  See [[glib-palette-subsystem]] and the Resource Manager (#127).

OPEN secondary: the dominant emitted codes 6/9 fold to group1 = ds[5] = set8 =
wood, yet mac_3d_start_e.png is all-STONE. Either that screenshot is a different
frame than this trace (the trace geometry is a left/centre band, x~16-32, NOT the
symmetric edge-to-edge corridor of the screenshot), or the wall-code->set mapping
needs review. Resolve AFTER the CLUT fix (render the real trace with a correct
CLUT and compare to the screenshot). Tools: /tmp/render_native.png (slots at
native scale), /tmp/tile8_sets.png (per-set tile8).

## Reproduce

```sh
make EXTRA_CFLAGS=-DFRUA_SKIP_ENTRY_EVENTS frua.prg
make gamedata DSN=HEIRS.DSN          # stages HEIRS save A + symlinks the prg
# run under Hatari -d data/work/gamedata; J200DIFF.TXT (recorded slots) +
# VIEWDIAG.TXT land on the GEMDOS drive. The skip-entry build auto-reaches the
# dungeon render.
```
