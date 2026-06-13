# Mac blit trace — HEIRS save A, level 5 dungeon entry (after the merchant)

Ground truth from the instrumented Mac binary (user, 2026-06-13), the frame
shown facing the dungeon right after the merchant event (took the 100pp +
ring). This is the reference for the port's render_3d_faithful at the HEIRS
start cell. Movement did NOT re-dump (the jt200 hook fired only on the
standing frame); text display is very slow in Hatari (the l435a slow-text
delay).

## Frame = 25 slots (repeats; #118..#142 is one frame, then re-runs)

| # | top | left | code | sub | blits |
|--:|----:|-----:|-----:|----:|---|
| 0 | 8032 | 8028 | 9 | 0 | far JT114 idx2 + near JT114 idx30 (8032,8032) |
| 1 | 8028 | 8028 | 9 | 9 | near JT114 idx1 (8028,8032) |
| 2 | 8024 | 8028 | 6 | 0 | near JT114 idx2 (8024,8032) |
| 3 | 8020 | 8028 | 6 | 9 | near JT114 idx1 (8020,8032) |
| 4 | 8016 | 8028 | 9 | 0 | far JT114 idx2 + near JT114 idx30 (8016,8032) |
| 5 | 8012 | 8028 | 9 | 9 | near JT114 idx1 (8012,8032) |
| 6 | 8036 | 8028 | 9 | 9 | near JT114 idx1 (8036,8032) |
| 7 | 8040 | 8028 | 6 | 0 | near JT114 idx2 (8040,8032) |
| 8 | 8044 | 8028 | 6 | 9 | near JT114 idx1 (8044,8032) |
| 9 | 8048 | 8028 | 11 | 0 | near JT999 idx2 (left8032 top8048) |
| 10 | 8052 | 8028 | 11 | 9 | near JT999 idx1 (left8032 top8052) |
| 11 | 8028 | 8024 | 2 | 1 | near JT114 idx13 (8028,8028) |
| 12 | 8008 | 8024 | 9 | 1 | far JT114 idx3 + near JT114 idx31 (8008,8028) |
| 13 | 8036 | 8024 | 9 | 2 | far JT114 idx4 + near JT114 idx32 (8036,8028) |
| 14 | 8048 | 8024 | 6 | 2 | near JT114 idx4 (8048,8028) |
| 15 | 8056 | 8024 | 11 | 2 | near JT999 idx4 (left8028 top8056) |
| 16 | 8016 | 8024 | 9 | 3 | far JT114 idx5 + near JT114 idx33 (8016,8028) |
| 17 | 8008 | 8016 | 6 | 4 | near JT114 idx6 (8008,8020) |
| 18 | 8028 | 8024 | 5 | 3 | far JT114 idx5 + near JT114 idx42 (8028,8028) |
| 19 | 8020 | 8016 | 1 | 4 | near JT114 idx6 (8020,8020) |
| 20 | 8052 | 8024 | 2 | 3 | near JT114 idx15 (8052,8028) |
| 21 | 8052 | 8016 | 6 | 5 | near JT114 idx7 (8052,8020) |
| 22 | 8040 | 8016 | 1 | 5 | near JT114 idx7 (8040,8020) |
| 23 | 7992 | 8016 | 1 | 6 | near JT114 idx8 (7992,8020) |
| 24 | 8048 | 8016 | 1 | 6 | near JT114 idx8 (8048,8020) |

## What this establishes (vs the tutorial trace in mac-blit-trace-3dview.md)

1. **JT114 dominates here.** Nearly every wall blits through JT114 (v-first,
   (top,left)); only wall code 11 uses JT999 (h-first). The tutorial frame
   used JT999 for its near set — HEIRS l5's art lives in the JT114 set.
2. **far/near idx pair strides:** code 9 = far idx N -> near idx N+28
   (2->30, 3->31, 4->32, 5->33); code 5 = far 5 -> near 42 (+37). Two
   strides, matching the tutorial note (+28 vs +37 by band).
3. **code -> blitter/role:** 9 = far+near JT114 pair; 6 = near JT114 only;
   11 = JT999; 2 = JT114 idx13/15 (a doorway/special face); 5 = far+near
   JT114 (+37 stride); 1 = near JT114 idx6/7/8 (the deep side walls).
4. **25 slots, not 18** — a fuller frustum than the tutorial's open corridor
   (this start cell has near side-walls on both sides -> a closed alcove).
5. Coord ranges: top 7992..8056, left 8016..8028. The 88x88 viewport in
   8000-space (x4 steps), same as the tutorial.

## Use

Instrument the port's jt200 with the SAME fields (#, top, left, code, sub,
each blit's JT114/JT999 + idx + coords), boot HEIRS save A, and diff this
25-slot frame slot-by-slot. Divergence in: slot count/order = jt199/l5b42;
codes = jt201/jt205 map reads; idx = jt200 selector; far/near pairing =
the +28/+37 stride logic; coords = l5b42 transform.

## Render-chain audit (2026-06-13) — vs the user's "10,8 facing east, stone, open L+R, door 1 step E"

GEO005 decoded directly (mirroring l7226's IFF parse): 19x19 map,
Wall1=5 Wall2=8 Wall3=1, HDR start marker[0] = **row=8, col=10,
facing=2 (East)**.

FINDINGS:
1. **Position & facing load CORRECTLY.** The port reads row8/col10/east
   from the HDR marker (HUD "10,8", facing east) — matches the real game.
   The earlier "isn't loading 10,8" hypothesis is WRONG; the location is
   right.
2. **No missing/stubbed functions in the render chain.** All full lifts:
   l7226 (GEO parse), jt199/l5b42 (frustum+transform), l5e52 (edge read),
   jt212 (automap edge), jt200/jt114/jt999 (slot render).
3. **l5e52 index = col*height+row (col-major) is ASM-FAITHFUL** — verified
   against CODE 7+0x5e52 (clamps arg1 vs ds[3]=height -> row, arg2 vs
   ds[2]=width -> col; index = col*ds[3]+row). NOT transposed.
4. Start cell (row8,col10) raw = 00 09 e6 00 00 1c:
   N=0x00 E=0x09 S=0xe6 W=0x00.  LOW nibble (l5e52, movement / first-person
   wall): N0 E9 S6 W0.  HIGH nibble (jt212, automap): N0 E0 S14 W0.
   - E low=9 = the DOOR ahead (east). W0/N0 open.  S low=6 = a wall on the
     right (south) — the one spot that doesn't match "open right" (a nibble
     nuance, or approximate recollection).
   - These LOW-nibble codes (9, 6, ...) are exactly the wall codes in the
     Mac jt200 trace above, so the Mac frustum ALSO keys off l5e52's low
     nibble — port and Mac read the SAME map data.

CONCLUSION: the wrong view is NOT a data-load or missing-function bug. It's
either (a) jt199/jt200 emitting different SLOTS than the Mac trace, or (b)
the wall TEXTURES — GEO005 wants wall set 5/8/1 (8X8DB), but
dungeon_view_setup loads DUNGCOM.TLB as a stand-in into g_wall_bmp; if
jt200_layer draws from g_wall_bmp (DUNGCOM) rather than the colour slots
(g_cw_*, 8X8DB set 5) that load_wall_groups fills, that's the wood-vs-stone
mismatch. DECISIVE NEXT STEP: capture the port's jt200 slot sequence and
diff against the 25-slot Mac trace above (hrdb breakpoint on jt200, since
the GEMDOS file-dump + --conout buffering both failed to land the log).
