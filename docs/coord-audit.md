# Coordinate-convention audit — the (v, h) migration worklist

**The root cause of every "menu/HUD text slightly off" issue and the
jt137 label mash: the Mac engine is VERTICAL-FIRST.**  QuickDraw's
`Point` is `{v, h}`, and THINK C passes coordinates in that order
through the whole engine.  Several port primitives were lifted
horizontal-first, and every caller since has compensated per call
site (arg swaps, `g_hud_dy` nudges, measured-not-derived chrome
positions).  jt137 (the faithful command-button DLItem method, lifted
2026-06-11) exposes the debt: it feeds (v, h) positionally into the
transposed primitives and the labels land transposed.

Update the status column IN THE SAME COMMIT as each fix.  Before
changing any primitive, re-verify its Mac arg order from the asm —
evidence cited per row.

## 1. Ground truth (from the Mac asm)

| primitive | Mac order | evidence | port today | status |
|---|---|---|---|---|
| jt1135 | axis-agnostic pair scaler | (v1,v2,out1,out2) — pure | same | OK |
| L0264 / pen slots | (v, h); -4898 = V, -4896 = H | CODE 5+0x264: jt1135(a,b,&-4898,&-4896); jt966 draws at -4896=X | jt1089 maps arg1 -> pen X (TRANSPOSED) | **FLIP** |
| jt1089 | (v, h, colour, fmt, ...) | = L024c+L0264+L0306; see L0264 row.  boot.c:7209 already documents the Mac order | (h, v) | **FLIP + swap port-convention callers** |
| jt1001 / jt995 / jt448 / L309c blit | (v, h, group, item) | CODE 5+0x31fc jt1005: arg1 -= metric y_bearing, arg1+height = extent; arg2 -= x_bearing | port l309c/l2d4e treats arg1 as X (jt216 passes screenX first and renders right) | **VERIFY l309c, FLIP + swap port callers (jt216 var names, l66e6 comment)** |
| jt1161 | (top, left, bottom, right, fill) | lifted that way; jt1086 passes (0,0,h,w) | same | OK |
| jt94 | (page, row, col, colour, fmt) | row before col | same | OK |
| jt103 | (top, left, right, bottom) | jt76 faithful call (1,1,38,22) | same | OK |
| DLItem rec[16]/rec[18] | rec16 = V, rec18 = H | jt137 asm: jt1089(scaled rec16+1, scaled rec18); l177a stream (8094=bottom row, 8056=h) | menu_run already packs rec16 = y ✓ | OK (the +4 baseline nudge in menu_run retires when jt137 goes live) |
| l2d3e method msg 2 | (v, h) | jt137 msg2 compares arg1 vs scaled rec16 = V | passes (mouse_y, mouse_x) ✓ | OK (depends on l31b8 row) |
| l31b8 mouse read | expect (&v, &h) — Mac GetMouse Point order | VERIFY from CODE 3 asm | port lift named (&mouse_y, &mouse_x) | **VERIFY** |

## 2. Caller inventory (the migration)

When jt1089 flips to faithful (v, h):
- **Straight after the flip (faithful positional lifts, currently
  transposed on screen):** jt137 labels, l59d6 apology screen, l4d98
  "Loading...Please Wait" banner, jt937/jt938 HUD text (re-verify the
  #113 `g_hud_dy` nudge — it may exist only to compensate), the boot.c
  7217 site (its swap becomes a pass-through — delete the swap).
- **Port-convention callers needing an arg swap:** hud_text (5236),
  the char-gen PICK screens (18569.., 19257..), the record-sheet rows
  (11841.., 12014..), the design picker (10505..), l66e6-style
  helpers, the jt148/l177a "Return" prompts — CLASSIFY EACH against
  its Mac asm before swapping; some are already faithful.
- Same exercise for jt448/jt1001/jt995 callers once l309c flips.

## 3. Verification protocol

Hatari screenshot per screen after each commit: main menu, Training
Hall, char-gen PICK screens, dungeon HUD (roster/clock/command bar),
area map, design picker.  The faithful end-state replaces
menu_draw_plates / menu_button_bevel / the per-screen nudges with
jt137's bars + labels (enable jt151 in l4d98 as the final step and
retire the stand-ins).

## 4. Related loose ends

- **l33ac numbered-name arm builds "bigpix.ctl0008.ctl"** (seen as a
  Hatari GEMDOS 8.3 clip warning): the level-2 skeleton appends the
  4-digit number AFTER the extension instead of before it ("bigpix"
  should become "bigpix008.ctl"-style per l16c6).  The clipped name
  then misses the file.  Fix the name builder against the Mac asm
  (CODE 6+0x33ac, the dual filename-format arms that were condensed).
- jt442(40) stays deferred in l4cc0 ("regressed ua_main" note) — the
  boot_a5_seed_defaults table seeding covers it; revisit when the
  dialog tier is fully faithful.
