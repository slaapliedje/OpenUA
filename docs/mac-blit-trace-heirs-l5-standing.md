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
3. **Blitter split MISSING**: code 11 (group 2 / Wall3) blits via **JT999**
   (h-first: left,top), all others via JT114 (v-first: top,left). The port uses
   jt114 for everything -> code-11 walls are transposed. Add the jt999 path in
   jt200 for the group-2/sel branch (asm L59d4 5a64: cmpiw #2,fp@(-2) -> JT999).
4. far/near idx strides (validates jt200 idx math): code 9 far N -> near N+28
   (2->30, 3->31, 4->32, 5->33); code 5 far5 -> near42 (+37); code 1/6 near only.

## Use
Re-lift jt199 (L6234) + add the jt999 blitter branch + restore the l5b42 axis
stash, validating the port's J200DIFF against this 25-slot frame slot-by-slot.
