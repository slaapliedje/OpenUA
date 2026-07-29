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
| L0264 / pen slots | (v, h); -4898 = V, -4896 = H | CODE 5+0x264: jt1135(a,b,&-4898,&-4896); jt966 draws at -4896=X | slot writes were already faithful | **DONE** (da11006) |
| jt1089 | (v, h, colour, fmt, ...) | = L024c+L0264+L0306; see L0264 row | FIXED: only the MoveTo read the slots crosswise; params renamed (v, h) | **DONE** (da11006) |
| jt1001 / jt995 / jt448 / L309c blit | (v, h, group, item) | CODE 5+0x31fc jt1005: arg1 -= metric y_bearing | AUDITED: port l309c was already vertical-first (arg1->sy, -y_bearing); only caller VAR NAMES mislead (jt216 "screenX" holds the v value, etc.) | **DONE — names only, no behavior change** |
| jt1161 | (top, left, bottom, right, fill) | lifted that way; jt1086 passes (0,0,h,w) | same | OK |
| jt94 | (page, row, col, colour, fmt) | row before col | same | OK |
| jt103 | (top, left, right, bottom) | jt76 faithful call (1,1,38,22) | same | OK |
| DLItem rec[16]/rec[18] | rec16 = V, rec18 = H | jt137 asm: jt1089(scaled rec16+1, scaled rec18); l177a stream (8094=bottom row, 8056=h) | menu_run already packs rec16 = y ✓ | OK (the +4 baseline nudge in menu_run retires when jt137 goes live) |
| l2d3e method msg 2 | (v, h) | jt137 msg2 compares arg1 vs scaled rec16 = V | passes (mouse_y, mouse_x) ✓ | OK (depends on l31b8 row) |
| l31b8 mouse read | (&v, &h) — Mac Point order | l2d3e feeds them to method msg 2 whose first arg jt137 compares against scaled rec16 = V; menu clicks land | OK (verified live: jt137 msg-2 hit-tests work) |

## 2. Caller inventory (the migration) — EXECUTED (da11006 + jt151 enable)

Un-swapped (their compensation comments retired): jt94's internal
call, l42a0, the shape-3 radio label, the l35f8 PICK headers, the
title_text macro.  Self-healed faithful sites: jt97, jt280, l53a6,
l5126, l52f2, the l59d6/l4d98 banners, jt137.  cg_draw (dead code,
unused attribute) left untouched.  jt151 is ENABLED — jt137 renders
every shape-1 item (menu/Hall verified: labels + hotkey letters +
key dispatch through the faithful chain).

Original inventory kept below for reference:

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

## Faithful menu chrome (2026-06-12, from the Mac menu blit trace)

DONE: jt81 lifted FULL (gen binder -> group 3 backdrop via new l384c +
FRAME edges 1-3(+4) + jt103 panel + jt174; trace-verified order);
menu_run faithful build (raw row coords — the +4 nudge retired, jt137
owns baselines; spacers = recessed DLItems with "" labels like the
Mac); menu_draw_plates RETIRED (the Mac draws no plates — the jt137
bar strips ARE the buttons).  Hatari: real GEN stone backdrop + labels
at the Mac rows (118/132/146/160/174/188).

FIXED (the "invisible bars") — the engine clip rect (-3054..-3050)
is A5-zero on a fresh boot, so bottom=0 clip-rejected EVERY l2d4e
blit on the menu (backdrop, edges and bars alike; the stone we saw
was the port fill, the text was DrawString which bypasses the engine
clip).  jt81 now seeds the clip full-screen when unset (the same
idiom the dungeon/area screens use).  The glyph data was never the
problem.  Original analysis kept below:

OPEN(retired) — the bar strips (FRAME items 10-15) blit invisibly: the GEN
backdrop (group 3 item 1) and the FRAME edges draw through the same
l309c/l2d4e path, so clip/pipeline are fine; items 10-15 differ by
metric flags 0xC5/0xC0 (bit 7 + bit 6 set; mode nibble 5/0, h=11,
bpp_w=1, x_bearing +8/0/-8).  Suspect l2d4e's handling of the high
flag bits (bit 7 'single-row'?) — item data is 88 bytes = 11 full
8-byte rows, so the repeat theory is wrong; check what the mode-5
arm does with these exact glyphs (dump pixels host-side via
tools/hlib_extract.py first — they may be mostly index 255 with the
face expected from a different CLUT range).

---

# 3. MAP-CELL axis convention (#100, 2026-07-29)

A different axis from everything above: not screen (v, h) but which of the
two **map** coordinates is which. This section exists because a wrong answer
here shipped in v0.5.12-beta.

## The invariant, straight from the asm

Two adjacent A5 bytes hold the party cell. Every faithful site treats them
in exactly one way, and it is not a matter of opinion:

| role | slot | delta table | bounds vs | in the cell index |
|---|---|---|---|---|
| A | `-12287` | `-11693` | `ds[2]` | the term **multiplied** by `ds[3]` |
| B | `-12288` | `-11684` | `ds[3]` | the term **added** (stride 1) |

    cell = ds + 290 + (A * ds[3] + B) * 6 + edge

Evidence, all cross-checked (do not re-derive from the DATA tables alone —
that is what went wrong):

- **`L5baa`** (CODE 7+0x5baa, 0x5bb4/0x5bd0): arg1 bounded by `ds[3]`,
  arg2 by `ds[2]`. So `jt210(arg1, arg2, dir)` takes B first, A second.
- **`L5bfa`** = jt210 (0x5c86..0x5c92): `a0@(3) * fp@(11) + fp@(9)` — arg2
  is the multiplied term.
- **`L23ee`** = jt312 (0x23f6): `-12287 * a0@(3) + -12288`, both
  sign-extended bytes.
- **`L1c3e`** = jt297 (0x1d6a..0x1db8): `d1 = -12287 + (-11693)[facing]`,
  `d2 = -12288 + (-11684)[facing]`, pushed `d2` then `d1`; and **`L1908`**
  (0x199a) stores its arg2 to `-12287`, arg3 to `-12288`.
- **`L14d8`** = jt292 (0x14ea/0x14fe/0x1566): the same two tables added to
  the same two axis roles, in an entirely separate function.

## What the NAMES say, and why you must ignore them

The port calls slot A "col" and slot B "row" in most comments, and
`ds[2]`/`ds[3]` "width"/"height". **The delta tables say the opposite.**
Indexed 1..8 (`l1908` normalises facing 0 to 8) the pair is a clean 8-point
compass ring — index 8 = N gives `(-1, 0)`, index 2 = E gives `(0, +1)` —
which makes `-11693` the ROW delta, and the Mac adds `-11693` to slot A.
By that reading **A is the row**, `ds[3]` is the width, and the map is
row-major.

Both namings are internally consistent; the engine does not care. **The
pairing is what matters**, and the pairing is the table above. Renaming
everything is a large, purely cosmetic diff that would risk exactly the
bug it looks like it prevents — left alone deliberately.

★ Where the names DO matter is `tools/geo.py` / `tools/dsn.py`: they label
the GEO file's axes independently, and a generated fixture inherits their
labelling. That is why a synthetic map cannot settle this question and why
#97/#98 went wrong on one. **Settle axis questions on an SSI-authored
module** (HEIRS), never on a generated one.

## Audit result

Every map-index site was checked against the invariant. Two defects:

1. **`jt297`** — v0.5.12-beta swapped the two tables (#98), inverting a
   faithful lift so the party stepped along the wrong axis. **Reverted**;
   the pairing now matches `L1c3e` byte for byte.
2. **`jt312`** (`boot.c` ~14957) — computed `-12288 * ds[3] + -12287`, the
   transpose of `L23ee`, and read both operands unsigned. It picked the
   floor/ceiling backdrop of a *different* cell on every square where the
   two coordinates differ, and could index past the map on a non-square
   one. **Fixed** — a real, pre-existing bug, and the only one the sweep
   found.

Verified clean (arithmetic matches the invariant): `l5baa`, `jt210`/`l5bfa`,
`jt201`, `jt212`, `jt197`, `l5fc0`, `jt236`, `jt292` (faithful to L14d8),
`jt311`, `l35de`, `l06e2`/`l0788` and the L3a1a/L4430 automap callers (those
were corrected in an earlier pass and their comments cite the Mac push
order), `l49dc_c11`'s `rec[1] * lvl[3] + rec[0]`.
