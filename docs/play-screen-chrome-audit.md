# Play-screen chrome — faithful-lift audit (the grey command-bar plate)

Goal: the grey command-bar plate behind the play screen's bottom bar (where
"Press [Return] to continue" and the dungeon command labels sit). The port
renders that bar as **stone**; the Mac renders it **grey** (lighter than the
panel grey), with a dark border. Reference: `data/press_return_to_continue.png`
(Mac), `data/port_press_return_to_continue.png` (port, stone).

## The faithful composer chain (read from the disasm)

- **`jt78` / `L67ca` (CODE 6+0x67ca)** — per-frame play-frame **repaint**. Calls
  `jt76`/`L670c` + dividers (`l66e6`) + viewport frame (piece 9) + compass
  (piece 21) + the facing compass face. This is what the port runs (via
  `port_draw_play_frame` → `l67ca`).
- **`jt76` / `L670c` (CODE 6+0x670c)** — `L38d0(0)` + **one** box
  `L4bf6(1,1,38,22)` + FRAME pieces 1,2,3,4 + `jt174`. The port's `jt76` matches
  this faithfully.
- **`jt77` / `L6920` (CODE 6+0x6920)** — the play-screen **setup** composer.
  Marked `__attribute__((unused))` in the port (never called). Draws:
  `L38d0(0)` + `L4bf6(23,1,38,21)` + `L4bf6(1,1,21,21)` + pieces 1,2,3,5 +
  [`jt1173(8093,8000,8100,8160)` clip → piece 4 → `jt1193`] + piece 6 + `jt174`.

## Box geometry (computed from L4bf6 → L3f88 → jt1161, all read)

`L4bf6(a1,a2,a3,a4)` → `jt1161(top=a2*4+8000, left=a1*4+8000, bottom=a4*4+8004,
right=a3*4+8004, 8)`; `jt1161` does `jt1135(top,left)→(y1,x1)`,
`jt1135(bottom,right)→(y2,x2)`. At play-screen scale 2 this gives a box
**x=[a1*8, a3*8+8], y=[a2*8, a4*8+8]**. So:

- `jt76`  `L4bf6(1,1,38,22)`  → native **(8,8)-(312,184)** — the whole upper area.
- `jt77`  `L4bf6(1,1,21,21)`  → native **(8,8)-(176,176)** — the **viewport** panel.
- `jt77`  `L4bf6(23,1,38,21)` → native **(184,8)-(312,176)** — the **HUD** panel.

**Conclusion: the command-bar plate is NOT in `jt76`/`jt77`/`jt78`.** Their two
`L4bf6` boxes are the viewport + HUD panels (y 8..176/184); none cover the
command-bar strip (native y ~186..198). The FRAME pieces (1..6) are border
edges/dividers; piece 4 clipped to the bar is a vertical 16x1154 *sliver*.

## So where does the grey bar come from?

Not yet located. Two candidates to chase next:
1. **Play-screen background + a mis-positioned FRAME bottom edge.**
   `port_draw_play_frame` already `memset`s the whole surface grey (index 21),
   so the bar *should* be grey — but it reads stone (index ~30), i.e. a stone
   FRAME horizontal edge (piece 2/3/5, all 184x30 / 170x30) is landing on
   y 186..198 and covering it. The Mac's bottom edge sits lower; the port's
   FRAME-piece bearing/positioning may differ. **Check the hotspots:** piece 3
   `hotspot=(0,-312)`, piece 5 `hotspot=(-7,-176)` — verify where the port blits
   these vs the Mac.
2. **The command-bar DLItem setup.** The normal dungeon bar (View/Take/Pool/…)
   is `jt452`/`jt449` DLItem buttons with bevelled plates; a base plate for the
   bar may be drawn by the play loop's command-bar build (`jt179`/`jt148`), not
   the chrome composer. Check how the *normal* command bar gets its background.

## Measured facts (don't re-derive)
- A direct grey fill of the bar (native y 186..198, x 8..312) **lands** on the
  surface (`port_draw_play_frame` probe: `px[192][120]==18` after the fill) but
  is **overwritten before present** — consistent with a stone FRAME edge (or a
  later compose) restamping the strip. Find that writer.
- The port's `jt103`/`L4bf6` arg→`jt1161` mapping is **faithful** (verified
  against the disasm) — not the bug.
- `jt77` is a faithful lift but **unused**; wiring it in draws the viewport+HUD
  panels (redundant with `jt76`), NOT the command bar — so it doesn't fix this.

## Status
The prompt **text** (position + colour) is fixed + committed (4c70719). The grey
command-bar **plate** is unresolved — it's a play-screen-chrome stand-in gap
(`port_draw_play_frame`), and the plate's faithful source is one of the two
candidates above. This wants a focused trace, not more guessing.
