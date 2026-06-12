# Mac-emulator blit trace — "Begin Adventuring" first-person view

Ground truth captured from the instrumented Macintosh release (real
binary, Mac emulator), 2026-06-12.  Four identical frames of 18
JT[200] calls each; one frame digested below.  This is the reference
for verifying the port's render_3d_faithful (jt199 -> l5b42 -> jt200)
output slot-by-slot.

## The 18-slot frame (frustum walk order)

| # | top | left | code | sub | blits (in order) |
|--:|----:|-----:|-----:|----:|---|
| 0 | 8032 | 8028 | 11 | 0 | near JT999 idx 2 (left 8032, top 8032) |
| 1 | 8028 | 8028 | 11 | 9 | near JT999 idx 1 (left 8032, top 8028) |
| 2 | 8036 | 8028 | 11 | 9 | near JT999 idx 1 (left 8032, top 8036) |
| 3 | 8048 | 8028 | 11 | 0 | near JT999 idx 2 (left 8032, top 8048) |
| 4 | 8052 | 8028 | 11 | 9 | near JT999 idx 1 (left 8032, top 8052) |
| 5 | 8028 | 8024 | 11 | 1 | near JT999 idx 3 (left 8028, top 8028) |
| 6 | 8036 | 8024 |  2 | 2 | near JT114 idx 14 (top 8036, left 8028) |
| 7 | 8056 | 8024 | 13 | 2 | far JT999 idx 4 + near JT999 idx 23 (left 8028, top 8056) |
| 8 | 8028 | 8024 |  6 | 3 | near JT114 idx 5 (top 8028, left 8028) |
| 9 | 8020 | 8016 |  6 | 4 | near JT114 idx 6 (top 8020, left 8020) |
| 10 | 8052 | 8024 | 11 | 3 | near JT999 idx 5 (left 8028, top 8052) |
| 11 | 8040 | 8024 |  6 | 3 | near JT114 idx 5 (top 8040, left 8028) |
| 12 | 8052 | 8016 | 10 | 5 | far JT114 idx 7 + near JT114 idx 44 (top 8052, left 8020) |
| 13 | 7992 | 8016 |  6 | 6 | near JT114 idx 8 (top 7992, left 8020) |
| 14 | 8020 | 8016 |  9 | 6 | far JT114 idx 8 + near JT114 idx 36 (top 8020, left 8020) |
| 15 | 8012 | 8012 |  9 | 7 | far JT114 idx 9 + near JT114 idx 37 (top 8012, left 8012) |
| 16 | 8048 | 8016 |  1 | 6 | near JT114 idx 8 (top 8048, left 8020) |
| 17 | 8048 | 8012 |  5 | 8 | far JT114 idx 10 + near JT114 idx 47 (top 8048, left 8012) |

## What the trace establishes

1. **Two blitters with OPPOSITE axis order.**  The instrumentation
   prints each callee's own arg order: `JT[999](left, top, ...)` is
   HORIZONTAL-first; `JT[114](top, left, ...)` is VERTICAL-first.
   Directly relevant to [[jt118-jt114-signature-mismatch]] and the
   (v,h) audit (docs/coord-audit.md): jt114 follows the Point rule,
   jt999 does not — verify the port's jt999/jt114 lifts against this
   before trusting either's variable names.

2. **Blitter selection is by frustum band, not wall code.**  Subs
   0-1 and some 2-3 rows go through JT[999] (the near wall set);
   deeper subs (3-8) through JT[114].  Wall code 11 always uses
   JT[999] (its art lives in the near set even at sub 3); codes
   2/6/9/10/1/5 use JT[114].

3. **far-then-near pairs.**  When a slot draws both (codes 13, 10,
   9, 5 at even subs), the FAR piece blits first, the NEAR piece
   second (painter's order).  Far/near idx pairs observed:
   4->23 (JT999), 7->44, 8->36, 9->37, 10->47 (JT114) — i.e. the
   near-set index = far index + 29 for the JT114 wall set at these
   slots (36-8=28, 37-9=28, 44-7=37, 47-10=37 — TWO strides: +28 for
   sub 6/7, +37 for sub 5/8; bands use different art rows).

4. **Coordinate ranges.**  top 7992..8056, left 8012..8032 — the
   88x88 viewport region in 8000-space (native x4 steps).  Slot 0's
   near column draws at left+4 (8028 -> 8032): the JT999 blit lands
   one 4-unit cell right of the jt200 left arg.

5. **18 calls/frame, 4x repeat** — the walk pumped four frames
   (present cadence), confirming jt199's 18-slot frustum and that
   jt200 re-runs the FULL slot list every frame (no caching).

## How to use it

Instrument the port's jt200 the same way (PROBE prints with the same
fields), boot the same design/position (Begin Adventuring start),
and diff the two traces.  Divergence points directly at the broken
piece: slot order = jt199, codes = jt201/jt205 map reads, idx = the
jt200 selector tables, coords = l5b42's transform.

## Menu / Training Hall capture recipe (the next trace wanted)

The menu never touches JT[200]/[999]/[114] — hook these instead
(same A5 jump-table slots as the existing hooks; THINK C args start
at 4(sp) at function entry, return address at (sp)):

| slot | CODE addr | args at entry | role |
|---|---|---|---|
| JT[1001] A5+0x1f68 | CODE 5+0x31ac | 4(sp)=top.w 6(sp)=left.w 8(sp)=group.w 10(sp)=item.w | THE glyph blit — every button bar piece, marker, frame bevel |
| JT[995] A5+0x1f38 | CODE 5+0x21fc | top.w left.w group.w item.w mode.w | the deep-mode blit variant |
| JT[1089] A5+0x2228 | CODE 5+0x0334 | 4(sp)=v.w 6(sp)=h.w 8(sp)=colour.w 10(sp)=fmt.l | every text draw (log the fmt string's first bytes if cheap) |
| JT[117] A5+0x03c8 | CODE 6+0x3994 | none | present/commit — a FRAME DELIMITER, prints once per redraw |
| JT[448] A5+0x0e20 | CODE 3+0x148a | top.w left.w group.w glyph.w | wrapper into 1001/995 — distinguishes jt137's button-bar pieces (it calls this directly) |
| JT[94] A5+0x0310 | CODE 6+0x3fd6 | page.w row.w col.w colour.w fmt.l | the row/col text wrapper |
| JT[137] A5+0x0468 | CODE 7+0x1234 | rec.l msg.w (args...) | the command-button method — fires via the -9282 method table, which holds the jump-table address, so the slot hook catches it |

Minimum set for the missing-10% question: **1001 + 1089 + 117**
(117 marks the frame boundaries the way the 4x repetition did in the
3D trace).  448 + 137 + 94 + 995 refine attribution.

Arm the hooks BEFORE the menu appears (during the "Loading...Please
Wait" banner) — the menu paints once, not per frame.  Capture: (a)
the main menu's first paint, (b) one hotkey press that repaints,
(c) the Training Hall.  Diff target: the port's same-screen PROBE
trail.

## Begin Adventuring FULL-SCREEN capture (user trace, 2026-06-12)

Second capture with the JT[1089] text hook armed — the complete play
screen paint around the same 18-slot JT[200] frame. This pins the
HUD text layer for task #114:

    (pressed Hall button repaint right before entry)
    v=175  h=168   colour=131 "Begin Adventuring" + 139 "%c"

    ROSTER BLOCK (8000-space, painted TWICE per entry):
    v=8008 h=8068  colour=140 "%s"     <- header left (NAME)
    v=8008 h=8132  colour=140 "%s"     <- header right (AC/HP)
    v=8016 h=8068  colour=139 "%s"     <- member row 1 name (139 = ACTIVE)
    v=8016 h=8132  colour=135 "%s"     <- row 1 AC
    v=8016 h=8148  colour=135 "%s"     <- row 1 HP
    v=8020 h=8068/8132/8148 colour=135 <- member row 2 (rows step +4)

    CLOCK / POSITION (8000-space, after the roster):
    v=8060 h=8104  colour=135 "%s"
    v=8052 h=8104  colour=135 "%s"

    [the 18-slot JT200 frame, identical to the digest above, x4]

    COMMAND BAR (RAW pixel space, v=189, after the frame):
    h=6 "Area"  h=46 "Cast"  h=86 "View"  h=126 "Encamp"
    h=182 "Search"  h=238 "Look"  h=278 "Inv"
    each colour 135 + a 143 "%c" hotkey overlay at the same (v,h)

Key facts: the roster/clock text is in 8000-space (jt94-routed,
scale-mapped), while the command bar is RAW v=189 — two different
coordinate regimes on one screen. Member rows step +4 units; the
active member's name draws colour 139, everything else 135, headers
140. The roster block paints twice per present (two passes), and the
whole screen sequence repeats per frame pump exactly like the JT200
slots.

## Saved-game slot picker (user trace, "Load Saved Game", 2026-06-12)

    JT1089 v=189 h=8004 colour=112 fmt="%s"        <- the prompt line
    JT1089 v=189 h=136  colour=135 "A" + 143 "%c"  <- slot buttons:
    JT1089 v=189 h=152  colour=135 "B" + 143 "%c"     real jt137
    JT1089 v=189 h=168  colour=135 "C" + 143 "%c"     DLItems on the
    JT1089 v=189 h=184  colour=135 "D" + 143 "%c"     bottom row

The menu's bottom-left "saved game letters" = an inline slot picker:
one prompt string + a lettered button per existing save (jt990/jt991
enumerate the SAVGAM files; see CODE 22+0x4cb2's enumeration loop).
(The first trace's pre-menu six text lines + the (189, 8032) "%s "
sequence were the COPY-PROTECTION manual-word prompt — the user was
typing the answer.  The port bypasses that screen by design, so
nothing there is missing.)  This is the next save/load UI piece
to lift (jt315's Load arm), AFTER which the menu screen is complete
to the last line.
