# Mac blit trace — HEIRS save A, level 5 STANDING START (10,8), ground truth

Captured by the user (2026-06-13) from the instrumented Mac binary at the
standing start cell (HUD 10,8), BEFORE any movement, WITH per-tile blit
positions. This is THE reference for the port's render_3d_faithful geometry.
The frame is 25 jt200 slots and repeats (#0-24, #25-49, ...).

## The 25-slot frame (jt200: top,left,code,sub + each blit's blitter/idx/coords)

| # | top | left | code | sub | blits |
|--:|----:|----:|-----:|----:|---|
| 0 | 8032 | 8028 | 9 | 0 | far JT114 idx2 (8032,8032) + near JT114 idx30 (8032,8032) |
| 1 | 8028 | 8028 | 9 | 9 | near JT114 idx1 (8028,8032) |
| 2 | 8024 | 8028 | 6 | 0 | near JT114 idx2 (8024,8032) |
| 3 | 8020 | 8028 | 6 | 9 | near JT114 idx1 (8020,8032) |
| 4 | 8016 | 8028 | 9 | 0 | far JT114 idx2 (8016,8032) + near JT114 idx30 (8016,8032) |
| 5 | 8012 | 8028 | 9 | 9 | near JT114 idx1 (8012,8032) |
| 6 | 8036 | 8028 | 9 | 9 | near JT114 idx1 (8036,8032) |
| 7 | 8040 | 8028 | 6 | 0 | near JT114 idx2 (8040,8032) |
| 8 | 8044 | 8028 | 6 | 9 | near JT114 idx1 (8044,8032) |
| 9 | 8048 | 8028 | 11 | 0 | near JT999 idx2 (left8032 top8048) |
| 10 | 8052 | 8028 | 11 | 9 | near JT999 idx1 (left8032 top8052) |
| 11 | 8028 | 8024 | 2 | 1 | near JT114 idx13 (8028,8028) |
| 12 | 8008 | 8024 | 9 | 1 | far JT114 idx3 (8008,8028) + near JT114 idx31 (8008,8028) |
| 13 | 8036 | 8024 | 9 | 2 | far JT114 idx4 (8036,8028) + near JT114 idx32 (8036,8028) |
| 14 | 8048 | 8024 | 6 | 2 | near JT114 idx4 (8048,8028) |
| 15 | 8056 | 8024 | 11 | 2 | near JT999 idx4 (left8028 top8056) |
| 16 | 8016 | 8024 | 9 | 3 | far JT114 idx5 (8016,8028) + near JT114 idx33 (8016,8028) |
| 17 | 8008 | 8016 | 6 | 4 | near JT114 idx6 (8008,8020) |
| 18 | 8028 | 8024 | 5 | 3 | far JT114 idx5 (8028,8028) + near JT114 idx42 (8028,8028) |
| 19 | 8020 | 8016 | 1 | 4 | near JT114 idx6 (8020,8020) |
| 20 | 8052 | 8024 | 2 | 3 | near JT114 idx15 (8052,8028) |
| 21 | 8052 | 8016 | 6 | 5 | near JT114 idx7 (8052,8020) |
| 22 | 8040 | 8016 | 1 | 5 | near JT114 idx7 (8040,8020) |
| 23 | 7992 | 8016 | 1 | 6 | near JT114 idx8 (7992,8020) |
| 24 | 8048 | 8016 | 1 | 6 | near JT114 idx8 (8048,8020) |

code multiset {1:4, 2:2, 5:1, 6:7, 9:8, 11:3}; group {0:7, 1:15, 2:3}.

## Findings (2026-06-13)

1. **Axis CONFIRMED**: near band steps `top` (8032->8012) with `left` constant
   (8028/8032). The blit is (top,left); blit-left = jt200-left + 4 (the per-layer
   step adds to LEFT). -> the l5b42/jt200 axis stash (top<-wide, left<-narrow,
   step on left) is CORRECT. Restore it.
2. **jt199 emission is NOT faithful**: brute-forcing the port's jt199 sim over
   all (row,col,facing) reproduces this 25-slot multiset at NO cell (closest is
   (13,8,0) = 27 slots, overlap 25). The port emits 16-21 slots with spurious
   codes 10/13 the Mac never emits. So L6234 was mis-lifted (the helper
   passes/depth limits/draw conditions differ). RE-DERIVE jt199 against THIS
   trace as the validation target.
3. **Blitter split is NOT a transpose** (CORRECTED 2026-06-13 from the L59d4/
   L31ac/L309c asm): jt200 dispatches code 11 (group 2) via JT[999]=L309c and
   all others via JT[114]=CODE6+0x3804. BUT JT[114] is a thin wrapper that calls
   JT[1001]=L31ac, which calls `L309c(left, top, jt468(*handle), idx)` — and the
   JT999 arm calls `L309c(left, top, JT[1004](), idx)`. So BOTH arms land the
   SAME pixels at the SAME (X=left, Y=top); there is no rotation/transpose. The
   ONLY real difference is the LIBRARY SOURCE: groups 0/1 use the -27894 handle
   table via jt468; group 2 uses JT[1004] (= g_a5_-4582) directly, bypassing the
   table. So do NOT add a transposing jt999 branch (that would INTRODUCE a bug).
   The group-2 fix is minor (3 edge slivers) and tangled with g_a5_-4582's dual
   use as the GEO read buffer — defer it; it is not the geometry bug.
4. far/near idx strides (validates jt200 idx math): code 9 far N -> near N+28
   (2->30, 3->31, 4->32, 5->33); code 5 far5 -> near42 (+37); code 1/6 near only.

## RESOLVED 2026-06-13: jt199 is FAITHFUL; the bug is GEO005 map loading

Full lift-verification of L6234 (CODE 7 @0x6234..0x6ea1) done (03167ef): the
port's jt199 + jt199_side/front/band match the asm for cases 1/0 + the L6e4a
tail exactly; case 2 had ONE divergence (right-side near-face must skip depth 0,
asm L65b2 `tstb depth; beq`) now fixed via a near_min param.

PROOF (offline replay): decoded the asm walk in Python and ran it against the
port's ACTUAL loaded map (dumped at runtime via j200_dump's cell-window) at the
real inputs row=10 col=8 facing=2(E), 19x19, walls 5/8/1 — it reproduces the
port's J200DIFF 16 slots EXACTLY (zero OOB reads). So the port's frustum walk is
a faithful L6234 and emits exactly what the faithful walk should for its map.

Therefore the 16-vs-25 gap is NOT a walk bug. The port's loaded GEO005 cells are
saturated with high nibbles (code 11 x40, 13 x4 within radius-4) and produce the
spurious codes 10/13; the Mac's 25-slot frame uses {1,2,5,6,9,11} (11 and 9 ARE
valid — so high nibbles per se aren't wrong, but 10/12/13 are absent). Since the
walk is faithful and inputs match, **the port's loaded map differs from the
Mac's** (or the trace's facing != E). The naive FORM/AMOD file read (ds@off 24,
cell@+290, (col*H+row)*6) does NOT reproduce the port's cells either — the engine
loads ds through a transform, so the file can't be compared raw.

NEXT (new task): confirm the port's loaded GEO005 ds cells vs the Mac's. Either
(a) the user dumps the Mac's loaded ds nibbles at the standing start, or (b)
confirm the trace's exact row/col/facing. Then fix the GEO load path. jt199 needs
NO further work.

## Diagnosis closed (2026-06-13): the bug is jt199's L6234 cell-walk

Verified faithful (do NOT touch): l5e52 (returns ds[290 + (col*H+row)*6 +
(dir&6)/2] & 15 = the raw cell-edge nibble 0-15, which IS the folded
group*5+style wall code); l5b42 (axis: jt200(top=y+wide<<2, left=x+narrow<<2),
step on left); jt200 (L59d4 code-fold by 5s + far/near idx math + the L309c
blit). The `code` arg to l5b42 is the raw l5e52 nibble (asm 6400/643e push
fp@(-22)=w or fp@(-24)=prev), so the codes are entirely determined by WHICH
CELLS jt199 reads.

Port J200DIFF (16 slots) vs Mac (25): port codes {13:2,9:1,2:2,11:6,10:1,1:1,
6:2,5:1}; Mac {1:4,2:2,5:1,6:7,9:8,11:3}. Port emits codes 10 & 13 the Mac
never does, 6x code-11 vs 3, 1x code-9 vs 8. -> jt199's reconstruction
(jt199_side/jt199_front/jt199_band + the sel 2/1/0 band setup at boot.c ~10093)
walks the WRONG cells. The fix is a faithful translation of L6234 (CODE 7
@0x6234, ~0x6234..0x6ea1, ~1086 asm lines): a JT[3] 3-case switch on a
view-layout selector (seeded moveq #2, iterated 2->1->0 by the L6e4a outer
loop), each case running depth bands with L5e52 reads + L5b42 emits. Replace
the guessed helpers with the asm CFG; validate J200DIFF == this 25-slot frame.
