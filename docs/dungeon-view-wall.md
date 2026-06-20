# Dungeon 3D-view worklist — the wall-tile geometry gap

## UPDATE 2026-06-19c — faithful L6234 lifted; walk is NOT the bug, PLACEMENT is
Replaced the parametrized jt199 reconstruction (jt199_side/front/band + the
bogus "empirical" axis swap DROW<-(-27853) bolted on at 996e126) with a
line-by-line transcription of L6234 (CODE 7 @0x6234..0x6ea1): the 3-band JT[3]
switch (sel 2/1/0), both side scans per band, exact soff signs/±1, depth gates,
the prev/carry side-face logic, and the L6e4a `back`-recede outer loop. Coord A
(l5e52 arg1, row/Y) steps by -27862; coord B (col/X) by -27853 — the swap is
GONE. Builds clean; emits 17 slots at 10,8,E, ~matching the asm-replay validated
at 03167ef (16 slots, codes {13,11,9,6,5,2,1} incl. 10/13 which ARE in the Mac's
own map data — cell (10,10) nibble 0x0D, mon-confirmed byte-identical).

DECISIVE re-read of mac-blit-trace-heirs-l5-standing.md (the CORRECTED top): at
10,8,E the **Mac itself emits ~16 slots, NOT 25** (the 25-slot frame is a
DIFFERENT position). So the prior "25 expected -> walk drops to 16/occlusion
overrun" framing (UPDATE 19b below) was chasing a phantom: 16-17 IS right, and
the walk + map are faithful. The visual scramble is one layer DOWN — the
slot->screen PLACEMENT/blit. Evidence: my 17 slots land at screen X={32,48,56}
(all left half) — which is what the Mac does too; on the Mac the pieces FILL
RIGHTWARD from their left anchor by their width. On the port they don't (white
gap right). Several slots map to Y=8/16 (ABOVE the 88x88 hole top at Y=24) =
vertical scatter. => chase per-piece Y-BEARING application in l2d4e/l309c (the
blit anchor), NOT the walk and NOT the CLUT (user-confirmed colours are CORRECT
2026-06-19 — do NOT touch the palette, it regressed badly last time). Also open:
a stray YELLOW SQUARE in the view's upper-left corner (possible unfaithful frame
draw). jt199 lift is DONE; superseded UPDATE 19b's "walk is the bug".

## UPDATE 2026-06-19b — J200DIFF re-analysed: the WALK is the bug (occlusion + L/R)
Captured a fresh J200DIFF.TXT at the natural 10,8,E start (HEIRS, GEO005) and
tabulated all 25 slots' SCREEN positions ((left-8000)*2, (top-8000)*2) + decoded
the radius-4 map cells. Two decisive, NEW findings:

1. **All 25 slots land LEFT of centre** — screen x ∈ {32, 48, 56} only (the 88px
   hole spans 24..112, centre 68). NOTHING is placed at x>56 → the right wall is
   absent (the white gap). Cause: the left- AND right-side scans use the SAME
   `left` globals (-12202/-12220 etc., all 0..4 → left 8012..8028), and EVERY
   piece's x-bearing is **0** (verified via tools/wall_extract.py: idx1/2/6/7/8/42
   all xbear=0). So there is NOTHING to separate left from right — the recession
   is entirely in `top`+soff (vertical), none in `left`. The corridor collapses to
   stacked left columns.
2. **Wrong wall SET (the user: "corridor is all stone = set5").** Most slots are
   grp=1 (wall2=set8=WOOD); the all-stone corridor should be grp=0 (wall1=set5).
   Decoding the cells: the cell directly ahead **(X11,Y8) is all STONE** (N=1,S=1
   side walls, E=5 far wall → all fold grp0). And E=5 is a WALL, so the visible
   corridor is ONE cell deep. But the port's walk origin is **2 cells forward
   (12,8)** and it reads PAST that blocking wall into (12,8)/(13,8) which ARE wood
   (E=9,S=9 → grp1) → it emits wood slots for OCCLUDED cells. => the walk lacks the
   occlusion STOP at a blocking wall (or the 2-forward origin + depth walk overruns
   it). [[party-coord-rowcol-convention]] row/col + the DROW/DCOL swap are suspects.

=> Both symptoms are in jt199/L6234 (the WALK), not the pieces (proven correct),
the transform (clean jt1135 (v-8000)*2; g_cwf_ox/oy are DEAD/unused), or the
bearings (all 0). NEXT: faithful L6234 lift focusing on (a) the per-cell occlusion
that stops a ray at a wall, and (b) where the Mac encodes left/right screen
separation (NOT the bearings, NOT the same-for-L/R globals — re-read L641a vs
L65b2 and the l5b42 axis for a per-side `left` term the reconstruction drops).
J200DIFF capture: `make EXTRA_CFLAGS="-DFRUA_HALL -DFRUA_SKIP_ENTRY_EVENTS"`,
boot+`b`+Up; C:\J200DIFF.TXT has the cells (w=) + slots.

## UPDATE 2026-06-19 — PIECE DECODE PROVEN CORRECT; bug is render geometry + CLUT
The port now starts NATURALLY at HEIRS 10,8,E (GAME-header start level 5 + GEO005
load via #128, 0ba5e51) — same frame as `data/mac_3d_start_e.png`, no mon-placed
party. Direct compare (/tmp/compare3d.png): the Mac is a clean stone corridor with
a centred WOOD door; the port has the door MISPLACED/sheared, fragmented stone, and
a WHITE GAP on the right (the 8 side-wall slots not landing — the 17+8 the user
describes).

DECISIVE: built `tools/wall_extract.py` (two-level GLIB decoder) and rendered the
real 8X8DB pieces at the natural frame (/tmp/wall_gray.png). The decode + the
two-level set/item navigation are **PERFECT**: idx1=8x8 solid, idx6/7=16x56
stone-brick WEDGES (clean transparent cutout = the receding corridor shape),
idx8=56x56 wall, idx42=door — all blen == w*h, sensible shapes. Set 1 & 5 = STONE
(indices 41..60), set 8 = WOOD (0..53); pieces index a SHARED dungeon palette, not
each set's 37-entry item-0. **So UPDATE 14i's "the bug is the PIECE PIXELS / 8X8DB
differs / l2856 navigation off" is REFUTED — the data and decode are correct.**

=> The remaining bug is purely the RENDER: (1) the 8 side-wall wedge slots
(idx6/7) don't land in the 88x88 hole — the jt199 reconstruction mis-places/clips
them (the doc's own recommended fix: faithfully LIFT L6234, structure mapped in
UPDATE 14h); and (2) the shared-CLUT model (each set's pieces are direct indices
into one ~0..65 dungeon palette that overlaps across sets — set5 stone 41..60 vs
set8 wood 0..53 — so the per-set 32-band rebase can't serve them; needs the Mac's
real per-level dungeon CLUT). NEXT: lift L6234 for the side slots; mon-dump the
Mac's loaded dungeon CLUT (clut 32+) for the palette. Inspect pieces with
`tools/wall_extract.py 8X8DB.CTL --sets 1,5,8 --gray --png out.png`.

## UPDATE 2026-06-14i — WHOLE pipeline cleared via J200DIFF; bug is the PIECE PIXELS
Built -DFRUA_SKIP_ENTRY_EVENTS, captured the port's 25 slots at 10,8,E (J200DIFF.TXT)
AND the user's Mac jt200 trace (#125-149). Slot-for-slot the port's top/left/code/
sub/far/near are BYTE-IDENTICAL to the Mac. Added landed-rect recording to the LIVE
l309c blit path (g_lc_x0/y0/w/h; l309c_tile is retired so the old tw/th=0 were stale):
the per-piece SCREEN rects are what the Mac's identical data + these pieces produce
(e.g. idx30 top=8032 -> y0=66 => ybear=-2, MATCHES the decoded spec). So slots,
idx, jt1135 transform, bearings, l2d4e blit+255-transparency are ALL faithful.
Yet /tmp/escher2.png at 10,8,E renders scrambled: a TALL GOLD/WOOD column at centre
(#0-10, all group1=Wall2=set8=wood) where mac_3d_start_e shows grey STONE; right half
(x>72) empty (no right wall). With identical inputs the only thing left that can
differ is the PIECE PIXELS: the port's loaded 8X8DB set8/set1 pieces (or l2856's
sub-GLIB idx->item navigation) don't match the Mac's. The whole geometry pipeline
(jt199 walk, jt200 fold, l5b42, jt1135, bearings, l2d4e) is CLEARED.
DIAGNOSTIC LEFT IN WORKING TREE (uncommitted, dump build only): the g_lc_x0/y0/w/h
recording in l309c + the FRUA_SKIP_ENTRY_EVENTS slot dump. Revert before any commit.
NEXT (decisive): pixel-compare ONE piece. Dump the Mac's loaded Wall2 handle
(A5-27890 -> jt468 -> GLIB) item idx30 (and idx6 wedge) bytes via mon, vs the port's
8X8DB.CTL set8 idx30/idx6 extraction (tools + a direct two-level GLIB decoder). If
they differ -> the gamedata 8X8DB differs from the Mac's OR l2856 navigation is off;
if identical -> a rendering step is still missing (re-examine JT1170/L2970 shear,
which memory calls a no-op).


## UPDATE 2026-06-14h — layout[22] mon-VERIFIED correct; full L6234 map (the lift target)
User mon-dumped -12240..-12198 (A5=01F74AC0, `m 1F71AF0 1F71B1B`):
  5,4,6,4,2,7,2,0, 9,5,4,3,3,3,1,1, 1,0,0,4,0,0
= BYTE-IDENTICAL to the port's hardcoded layout[22], INCLUDING the zeros at
-12226/-12206/-12204/-12200/-12198. So the zeros are REAL on the Mac — the layout
table is CORRECT. The Escher is NOT a coordinate bug; it is the port's jt199
RECONSTRUCTION mis-using the (correct) globals.

FULL L6234 STRUCTURE (mon + disasm, the faithful lift target). jt199(Y=fp8, X=fp10,
row=fp13, col=fp15, facing=fp17). dir aliases: L=(facing+6)&7 fp@-1, R=(facing+2)&7
fp@-2, B(ack)=(facing+4)&7. drow=-27862[dir], dcol=-27853[dir]. ORIGIN
fp@-6/-8 = (row,col) + 2*delta[facing], set ONCE (NOT receding!). JT[3] selector
fp@-21 = 2->1->0 via L6e4a (3 depth bands, FIXED forward origin; depth differs by
the soff horizontal spacing + sideways spread, NOT a receding forward cell — THIS
is where the port diverges: port_jt199 RECEDES orow each band).
  * CASE 2 (L63a2, far, soff step -2, FOUR scans from origin directly):
    - L63be  left-side : walk L depth0..3; near-face -12240+soff/-12220 style0
              (depth<3); side -12222+soff+**1**/-12202 style9 (when prev>0).
    - L6556  right-side: walk R depth0..3; near-face -12240+soff/-12220 style0
              (depth1..2, skip0); side -12222+soff-**1**/-12202 style9.
    - L66f2  front-L   : walk L depth0..3; -12238+soff/-12218 style1 (depth0 plain,
              else -1). soff +2.
    - L67e2  front-R   : walk R depth0..2; -12236+soff/-12216 style2 (depth0 plain,
              else +1). soff +2.
  * CASE 1 (L68be, mid, soff0 -6 step +3, TWO scans, start = origin + 2*delta[L|R]):
    - scan1: start O+2L, walk R depth0..2; facing -12234+soff/-12214 style3;
             side(read L) -12232+soff/-12212 style4 (depth>0).
    - scan2: start O+2R, walk L depth0..2; facing -12234+soff/-12214 style3;
             side(read R) -12230+soff/-12210 style5 (depth>0).
  * CASE 0 (L6bc2, near, soff0 -7 step +7, TWO scans, start = origin + 1*delta[L|R]):
    - scan1: start O+1L, walk R depth0..1; facing -12228+soff/-12208 style6;
             side(read L) -12226+soff/-12206 style7 (depth>0).
    - scan2: start O+1R, walk L depth0..1; facing -12228+soff/-12208 style6
             (depth0 only); side(read R) -12224+soff/-12204 style8 (depth>0).
Styles 0..8 = the jt200 (code,sub) layer; 9 = the inter-cell side wall.
NOTE the case-0 side faces (style7/8) DO use the zero globals -12226/-12206 &
-12224/-12204 — legitimately (those slots are sparse). The big VISIBLE near side
walls come from case-2's style-9 side scans (-12222/-12202) + case-0 style6/7.

CORRECTION (verified L6e4a): the Mac DOES recede the origin — L6e4a adds
delta[back] to fp@-6/-8 each band (2fwd -> 1fwd -> party) and decrements the JT[3]
selector fp@-21. The port's jt199 already recedes the same way. Re-checking every
arm: the port jt199 + jt199_side/front/band MATCHES L6234 structurally — origin
recede, case-2 4 scans (2 side depth0..3 + 2 front depth0..2), case-1/0 2 scans,
the globals (-12240/-12222/-12238/-12236 ; -12234/-12232/-12230 ; -12228/-12226/
-12224), styles (0/9,1,2 ; 3,4,5 ; 6,7,8), depth gates, soff0/steps (-2 ; -6,+3 ;
-7,+7), and the L/R near-face skip-0 + the +1/-1 on the style-9 side faces ALL
line up. So jt199 (the WALK) is NOT the bug.

=> The Escher is DOWNSTREAM of jt199: l5b42 (per-slot 8000-space transform +
placement) or jt200 (the (code,style)->piece-idx pick + far/near blit), or l5e52
(edge read) returning wrong values for the near cells. The 8 "missing" near slots
are emitted by jt199 but l5b42/jt200 drop or mis-place them. layout[22] is correct
and jt199 is faithful — both now CLEARED.
NEXT: slot-level diff — build -DFRUA_SKIP_ENTRY_EVENTS (enables the g_j2 record +
j200_dump -> C:\J200DIFF.TXT: per slot #,top,left,code,sub,group,farIdx,nearIdx)
at the 10,8,E frame, and diff vs the Mac 25-slot trace. That pinpoints whether the
divergence is the transform (top/left), the piece pick (idx), or the cell read.

## UPDATE 2026-06-14g — ESCHER ROOT CAUSE: hardcoded layout[22] has ZERO entries
User reframed the symptom (decisive): the Mac draws **25 slots**; the port draws
only **17, mis-ordered** (the Escher), and the **8 near left/right side-wall
slots are NOT drawn at all**. Captured the live port view at 10,8,E
(/tmp/escher_now.png) vs data/mac_3d_start_e.png: port has a coherent left wall +
far door but scrambled centre + missing right/near side walls; Mac is a clean
symmetric corridor.

ROOT CAUSE FOUND: jt199 (boot.c ~10332) is a PARAMETRIZED RECONSTRUCTION of
L6234 (jt199_side/jt199_front/jt199_band), NOT a faithful lift. It reads the
per-slot screen deltas from the frustum LAYOUT globals -12240..-12198 (22 words).
Those are **HARDCODED from a single live mon capture** (boot.c ~8203 `layout[22]`)
that overwrites the DATA image unconditionally — and SEVERAL ENTRIES ARE 0:
  -12226=0 (k7), -12206=0 (k17), -12204=0 (k18), -12200=0, -12198=0.
jt199's nearest band (sel==0, origin = party cell, the BIG near corridor walls)
reads gyB/gxB = -12226/-12206 (left) and -12224/-12204 (right) for the SIDE
faces -> X=0/Y=0 -> blit lands outside the 88x88 viewport (24,24,112,112) ->
clipped -> the 8 near side slots VANISH. The capture missed them (those slots
weren't active in the captured frame, so mon read stale 0s).

The Mac WRITES -12240..-12198 via an indexed pointer (no literal-displacement
write in the disasm; the CODE_02 -12194 writes are a SEPARATE text-input buffer).
The init that computes them is unlocated.

NEXT: (a) user mon-dumps the real -12240..-12198 (22 words) at the standing frame
-> replace the bad capture / seed the zeros; THEN (b) faithfully LIFT L6234 to
replace the jt199_side/front/band reconstruction (fixes the 17 mis-ordering too).
The L6234 arms: JT[3] selector fp@-21 = 2->1->0, origin recedes 1 cell/pass
(start = party + 2*facing-delta); case2=L63a2 (far, -12240/-12222 side via L5b42
styles 0/9, +1 left / -1 right on the style-9 face), case1=L68be, case0=L6bc2
(near). L641a/L65b2 = the left/right side sub-loops.


## UPDATE 2026-06-14e — SHELL lift VERIFIED on HEIRS; post-event wall loss = FC/CLUT
Live `make run-game DSN=HEIRS.DSN` (party 4,4 level 5). Results:
- **Shell lift is SOUND.** Pre-event the view shows the 17-frame Escher walls WITH
  the L57f2 region fills behind them, and **no freeze**. So L57f2 composes
  correctly with the wall walk; the QuickDraw-PaintRect freeze (UPDATE 14d, first
  cut) was fixed by drawing the regions with direct map_px (jt1161's real
  L053e/L0888 pixel-fill semantics), not PaintRect.
- **The TUTORIAL "freeze" was a CONFOUND, not the shell.** `make run-game` defaults
  to `DSN=TUTORIAL.DSN`, which ships ONLY GEO040 (+GAME/STRG); the shipped save
  targets level 40 -> "saved level GEO missing, keeping default; lv=14" and
  TUTORIAL has no GEO014 either -> no map -> jt199 walks nothing + the missing-
  level path stalls. ALWAYS test the dungeon view with `DSN=HEIRS.DSN` (#100).
- **Post-event regression (the real target).** After the Merchant/Event dialog the
  re-rendered view shows ONLY the region fills (white sky / grey floor), NO walls.
  This is the merchant-PIC clobber (#125-era / #129), NOT the L57f2 change: the
  jt1069 picture-palette install DOES set g_clut_clobbered (boot.c:42690) so the
  wall CLUT bands reinstall — but the wall ART handles (-27894/-27890/-27886,
  loaded by l6148/l6eea) get evicted from the shared FC/heap pool by the merchant
  picture load, and l6148 skips reload when the handle ptr is stale-non-zero. The
  fix is the shared-palette CLUT + FC-pool eviction model (#127), same blocker as
  the gated backdrop overlay (g_dungeon_bigpic_overlay).

STATUS: the faithful L57f2/L58c4/L6ea2/L5fc0 lift is a clean, verified unit ready
to commit. NEXT = the FC-pool/CLUT unification: (a) make l6148 reload when the FC
pool evicted the wall record (don't trust a stale non-zero handle), and/or (b)
move the dungeon to the faithful SHARED dungeon palette so the merchant/backdrop
picture palette and the wall sets stop clobbering each other -> then flip
g_dungeon_bigpic_overlay = 1.

## UPDATE 2026-06-14d — FAITHFUL perspective SHELL lifted (L57f2/L58c4/L6ea2)
Lifted the dungeon-view perspective shell from the CODE 7 disasm, replacing the
flat `g_back_img` stand-in fill in `render_3d_faithful`. The shell is the missing
piece behind the "left/right not drawn at all" complaint: the Mac draws the
backdrop regions + perspective image FIRST (L57f2 in jt221, before L6234), then
the walls on top. The port only had walls + a flat fill.

Lifted faithfully (every arm from CODE_07.s):
- **L5fc0** (0x5fc0): party-cell BackdropZone selector = `ds[290+(col*H+row)*6+5] & 3`.
- **L6ea2** (0x6ea2): `jt113(50)` + `l33ac("back1", id, 0, -12294, &-22222)` — the
  SAME FC binder the wall sets use; resolves to shared BACK.CTL on HEIRS (no
  per-design Back1NNN override).
- **L58c4** (0x58c4): zone -> `ds[8+zone]` backdrop id, the night-variant nudge
  (id>=32 & hdr[8] outside [6,20) -> id++), cache vs -12289, on change
  `jt105(=L3f3c)(32,255)` + L6ea2; then `jt121(2,2,1,0,-22222)` (native arm;
  the deep `jt118` arm is unreachable while g_a5_2347!=0) + `jt124` palette commit.
- **L57f2** (0x57f2): jt1200()==3 -> 3x JT[1161] screen-space fills; else 3x
  JT[116] cell-space fills (sky -12294 / middle -12292 / floor -12291) + L58c4.

Wiring: `render_3d_faithful` now calls `l57f2()` inside the viewport clip (before
the l6148/jt199 wall walk). The CODE 11 level-enter (L41a0: `jt217(15,0,0,8)`) that
populates the fill colours is NOT yet lifted, so render_3d_faithful seeds those
faithful defaults when -12294/-12292/-12291 are all zero. **Builds clean
(soft-float). PENDING: live `make run-game` verification** — expect solid
sky/floor regions in the hole (indoor HEIRS: ds[8+zone] likely 0 -> L58c4 no-ops,
no perspective image, no CLUT conflict with the wall bands). NEXT if regions are
black: confirm -12294 seed reaches the regions / lift L41a0 properly.

## UPDATE 2026-06-14c — SCALE + ANCHOR fixed; positions now BYTE-IDENTICAL to Mac
Three faithful fixes this session (commits 0b1e0a1, 526935d):
1. **jt1135 x2 position scale restored** (l5b42). The port pre-remapped to native
   ((v-8012)+oy = delta*4), making the downstream jt1135 no-op and DROPPING its
   x2; slots packed into delta*4 (~36px, left half) = the flat wall. Restored the
   real (v-8000)*2 -> delta*8 (~72px), fills the 88px pane. Flat wall -> corridor.
2. **X anchor 8016 -> 8012** (render_3d_faithful jt199 call). The real L6234 caller
   (CODE_10 0x20e0) passes 8012 for the LEFT base (fp@10); 8016 put every slot +8px
   right (uniform across all 25).
3. **jt200 layer step +4 -> +8**. The asm's +4 is 8000-space, pre-jt1135-x2; in the
   port's pre-scaled screen space it must be +8.
RESULT: the port's emitted (top,left) for ALL 25 slots are now BYTE-IDENTICAL to
the Mac standing trace via (v-8000)*2 (max |dTop|=0, |dLeft|=0). The door / left
wall / stone floor place correctly (user-confirmed). The slot GRID is solved.

### Tile scale: x1 (native) is correct; x2 OVER-fills
Tried scaling the 8x8 tile PIXELS x2 in l309c_tile to fill the x2 grid (to match
L2970's jt1200 lslw). Result: a SOLID wall covering the sky/corridor opening
(worse). Reverted. Tiles ARE native 1:1 (VGA-authored; the Mac port is from DOS).
So the remaining per-tile misplacement is NOT a uniform scale.

### REMAINING (the "Escher"): per-tile bearing / far-near + wood-vs-stone
With positions Mac-identical and tiles 1:1, the residual wrongness (floating wood
panels upper area, right-side fragments) is:
  (a) per-piece BEARING and/or the far/near layer pairing (jt200 draws a far face
      idx + a near face idx; the 8x8 pieces land at pos-bearing -- verify the
      bearing read + the far/near idx pairing per slot vs the Mac trace's
      "blits" column, e.g. slot0 "far idx2 + near idx30 (8032,8032)");
  (b) WOOD vs STONE: emitted codes 6/9 fold to group1=ds[5]=set8=wood (faithful to
      the Mac trace codes), but data/mac_3d_start_e.png shows stone. OPEN: is that
      screenshot the SAME frame as the jt200 trace, or post-move? Needs the user's
      Mac. If same frame, the wall-code->set fold (ds[group+4]) needs review.


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
