# Function reference

A running catalog of the Mac functions we've reverse-engineered, what they do,
where they live, and the traps. **The point of this file: stop re-deriving the
same thing every session.** When you trace a function and understand it, add a
line here. When you find a function you need, look here first.

> **Two companion docs:** this file holds the *curated* deep-dives (subsystems,
> ABIs, traps). For a *complete, auto-generated* list of every function in the
> engine + shim with its origin (`CODE N+0xXXXX`/`JT[N]`) and one-line purpose,
> see **`docs/function-index.md`** (regenerate: `python3 tools/function_index.py`).
> **Grep the index before writing any new function** — it's the fastest way to
> avoid re-implementing something already lifted (the recurring trap: a faithful
> `jtN`/`lXXXX` already exists but a port stand-in was written instead).

Conventions:
- `jtNNN` = a JT (jump-table) entry. **JT slot address = `a5@(34 + 8·N)`** — e.g.
  `JT[452]` lives at `a5@(3650)`. Reverse: a DLItem method pointer of `a5+1130`
  decodes to `JT[(1130-34)/8] = JT[137]`. This is how you identify a function
  from a raw method pointer in a `mon` dump.
- `lXXXX` = a CODE-local helper at hex offset `0xXXXX` in its segment.
- "CODE N + 0xXXXX" = the segment + offset the disassembly shows.
- Port name = the C symbol in `src/engine/boot.c`.

---

## DLItem dialog / button system (CODE 3)

This is the heart of every menu, list, and button in the game. A **DLItem** is a
32-byte record; a dialog is an array of them, painted and hit-tested by their
per-shape *method* pointer.

### Record layout (32 bytes)
| off | field | set by |
|-----|-------|--------|
| +0  | method ptr (the shape handler) | shape-alloc cmd, from `g_a5_-9282[shape-1]` |
| +4  | action proc (long) | cmd 34 |
| +8  | (long) | cmd 35 |
| +12 | label ptr / value (long) | shape-alloc, or cmd 39 |
| +16 | v (row pen) | shape-alloc, or cmd 40 |
| +18 | h (col pen) | shape-alloc, or cmd 40 |
| +20 | (short) | cmd 41 |
| +22 | (short) | cmd 42 / shape-2 alloc |
| +24 | **count** (short) — bevel-strip width / hit width | cmd 36 |
| +26 | **style_size (`ss`)** (short) | cmd 37 |
| +28 | flags | cmd 16..22 set bit, 24..30 clear bit |
| +29 | accelerator char | cmd 32 |
| +30 | (byte) | cmd 33 |
| +31 | colour | cmd 38 |

`flags` (+28): bit0 selected/pressed, bit1 disabled (no draw), bit2 dim,
bit3 focus, bit7 painted.

### Functions
- **jt442** (CODE 3) — DLInit: seed `g_a5_-9282[0..6]` with the 7 shape handlers
  + pool base. **Port: DEFERRED** (calling it regressed `ua_main`); the port
  seeds the table directly in a boot helper (`boot.c` ~line 8395) instead.
- **jt447** (CODE 3+0x298a) — dialog reset: `g_a5_9254 = g_a5_9286` (pool base),
  count=0, manager active=1. Called at the start of every dialog open.
- **jt450(slot, method)** (CODE 3+0x2950) — install `method` into shape table
  `g_a5_-9282[slot-1]`, **return the previous handler**. Returns prev even if
  `method==0` (query). This is how a screen overrides a shape handler.
- **jt452(shape0, ...)** (CODE 3+0x29a0) — the variadic **DLItem stream
  installer**. Stream = `(cmd, args...)` tuples, terminated by `cmd 0`.
  - `1..7` alloc a DLItem; method = `g_a5_-9282[shape-1]`. Arg shape: 1,3,4,6 →
    2 shorts (v,h) + 1 long (label); 2 → 1 short; 5 → 4 shorts; 7 → 1 long.
  - `8` alloc with a **caller-supplied method** (next long).
  - `16..22` set bit (cmd-16) of rec[28]; `24..30` clear bit (cmd-24).
  - `32→rec[29]`, `33→rec[30]`, `34→rec[4]`, `35→rec[8]`, `36→rec[24]`,
    `37→rec[26]=ss`, `38→rec[31]`, `39→rec[12]`, `40→rec[16]+[18]`,
    `41→rec[20]`, `42→rec[22]`.
  - **Decode site streams with `tools/jt1_extract`-style care**: in the asm the
    args are *pushed in reverse*, so read the C arg order bottom-up.
- **jt453** (CODE 3+0x2cd4) — run the modal: `for(;;) l2d3e()` blocking poll;
  draws the DLItems once via `l30ba`. (Port adds a `qd_present()` after the
  one-shot draw so the frame publishes.)
- **jt449 = l2c60** (CODE 3+0x2c60) — the per-frame DLItem repaint walker.
- **jt444(item,a,b,c)** (CODE 3+0x3056) — dispatch one DLItem's method by index.
- **l30ba(start,end,cmd)** (CODE 3) — invoke `method(rec, cmd, 0, 0)` for a
  range of items (paint walk).

### Shape handlers (`g_a5_-9282[0..6]`, indexed by shape-1)
The boot default table is `jt382, jt381, jt380, jt379, jt378, jt377, jt376`
(slots 0..6). **BUT screens override slots.** Confirmed real slots from a `mon`
dump of the char-gen pick screen: slot0 = **JT[137]** (not jt382!), slot2 =
JT[380].

- **jt382 = L1a5e** (CODE 3+0x1a5e) — *single-glyph* shape-1 painter and the
  boot **default**. `ss>=0` → one `L148a(rec16,rec18, style, size+hl)` glyph
  (ss==0 ⇒ style 0, size 14 ⇒ **ALWAYS.CTL item 14, a down-arrow**). `ss<-1` →
  a bevel strip (left-cap + `rec[24]` middle + right-cap) — *the port stubs this
  arm out*. The port also bolts a `DrawString` label on (the Mac L1a5e draws no
  text itself).
- **jt137 = JT[137] = CODE 7+0x1234** — the **real beveled button painter**.
  Installed into shape slot 0 by **jt151** (see below). Draws:
  `jt448(y,h,1,pal+10)` left cap, `for i<rec[24]: jt448(y,h+i*4,1,pal+11)`
  middle tiles, `jt448(y,h+(i-1)*4,1,pal+12)` right cap — i.e. the **FRAME.CTL
  bevel pieces (items 10/11/12 normal, 13/14/15 highlighted)** — then the label
  via `jt1089`. `pal = (rec[28]&9)?3:0` selects the highlighted variant.
- **jt151 = CODE 7+0x1572** — registers the beveled button: calls
  `JT[450](1, jt137)` (→ shape slot 0 = jt137) and stores the previous handler
  in `g_a5_-12918` for chaining. **This runs inside `l4d98`** (the game-session
  init), so any path that bypasses `l4d98` loses the bevels (buttons fall back to
  plain `jt382`). *This was the whole "DONE/EXIT have no bevel" bug* — the
  `FRUA_CHARGEN` harness jumped in before `l4d98`; it now runs after it. NB:
  there is **no `l4d98` hang** — the earlier symptom was a bare `make run` with
  no gamedata mounted (design load had nowhere to go); `run-game` reaches the
  menu cleanly.
- **jt380 = JT[380] = CODE 3+0x224c → l14d0** — shape-3 (list-row / radio item)
  painter. Draws the marker glyph via `l148a(rec16,rec18,0,16+hl)`
  (ALWAYS.CTL items 16..20 = the radio discs) then the row label via
  `jt1089(v+offset, h, col, "%s", label)`. The port adds a small baseline drop
  to centre the 8px label on the 7px marker.
- **jt448(y,x,group,glyph)** (CODE 3+0x148a area) — bevel/marker tile blit:
  `jt1200()==3 ? jt995(...,2) : jt1001(y,x,group,glyph)`. For buttons
  `group=1` ⇒ FRAME.CTL.

---

## GLIB art / glyph blit (CODE 5)

GLIB = the `.CTL`/`.TLB` tile-library container. `'GLIB'` magic; u16 count at
+8; `count+1` u32 offsets at +16 (entry 0 = 16-colour palette, 1.. = tiles).
Each tile: 8-byte header then pixels.

**Tile header (8 bytes, big-endian)** — *engine reading* (`l309c`):
`+0 height(u16), +2 ybear(s16), +4 xbear(s16), +6 bpp_w(byte), +7 mode(byte)`.
`pix_w = bpp_w*8`. Pen is placed then backed off by the bearings
(`top = jt1135(coord) - ybear`). `mode & 15`: 0 = 1bpp mono (OR fgColor), 2 =
PackBits 8bpp, 5 = raw 8bpp (when `mode&0x40`), 9 = composite, 3/7/10 deferred.
*(NB: `tools/hlib_extract.py` misreads these UI tiles as width-first 1bpp+mask —
trust the engine reading, not the tool's `WxH`.)*

- **jt468(group)** (CODE 5) — resolve a GLIB group base. Port: group 0 =
  ALWAYS.CTL, group 1 = FRAME.CTL (resident); others via the FC pool.
- **l37aa(base, idx)** (CODE 5+0x37aa) — GLIB item lookup: verify magic,
  bounds-check, return `base + offset[idx]`.
- **l2856(handle, size, out8)** (CODE 5+0x2856) — copy item `size`'s 8-byte
  metric header, return pointer to its bitmap (`entry+8`).
- **l309c = JT[999]** (CODE 5+0x309c) — UI glyph blit entry: remap pen via
  `jt1135`, fetch item metric, back off bearings, hand bitmap to `l2d4e`.
- **l2d4e(src, bpp_w, height, y, x, flags)** (CODE 3) — the actual blit + clip
  (clip rect = `g_a5_-3054/-3050/-3056/-3052` = top/bottom/left/right). Handles
  mode 0 (mono), 2 (PackBits via `jt1171`), raw 8bpp (`flags&0x40`).
- **jt1001(a,b,c,d)** (CODE 5+0x31ac) — `l309c(a,b, jt468(c), d)`: blit group
  `c` item `d` at pen (a,b). **Shallow-mode path.**
- **jt995** (CODE 5+0x21fc) — the deep-mode (640×400) equivalent of jt1001.
- **l148a(top,left,style,size)** (CODE 3+0x148a) — picks deep vs shallow:
  `jt1200()==3 ? jt995(...,2) : jt1001(top,left,style,size)`. `style`=group,
  `size`=item. **It's a glyph blit, not a font draw.**
- **jt1171(src,dst,nbytes)** (CODE 5) — PackBits/`UnpackBits` decoder; returns
  advanced src so rows decode in sequence.

---

## Coordinate / text primitives

- **jt1135(v1,v2,&o1,&o2)** (CODE 4+0x77fe) — logical→screen transform.
  `scale = (g_a5_-2347==0) ? 3 : 2`. Values `>6000` are treated as `8000`-anchored
  and become `(v-8000)*scale` px; `<=6000` pass through. **The port runs the
  320×200 path (scale 2) only.** So Mac build-coords (e.g. 8094) are authored for
  both ×2 and ×3; never re-hardcode them — compensate in the painter if needed.
- **jt1089(v,h,colour,fmt,...)** (CODE 5+0x334) — formatted text paint at
  logical (v,h). `colour & 0x0f` → pen via the QuickDraw shim; the shim is
  baseline-anchored (glyph body *above* y) where Mac coords are text-top, so
  text lands ~ a few px high vs Mac — painters add a small `+dy`.
- **jt1161(top,left,bottom,right,fill)** (CODE 3) — clipped PaintRect / box fill;
  147 callsites (frame borders, row erases, overlays).
- **jt103(top,left,right,bottom)** (CODE 6+0x4bf6) — content-panel box fill
  (`jt1161` mode 8). Does **not** set the clip.

---

## Screen chrome

- **jt76** (CODE 6+0x670c) — standard panel chrome: `jt108(0)` clear,
  `jt103(1,1,38,22)` panel box, then `jt1001(8000,8000,1,1..4)` = FRAME.CTL edge
  glyphs (item 1 = top bar, 2 = left, 3 = right, **4 = bottom stone molding** at
  y=184), `jt174`. **All four edges unconditional** (cf. `jt81` which gates item
  4 on deep mode). Needs the clip seeded full-screen first (`jt108` does *not*
  seed it; `jt81` does — bypassing `jt81` can leave edges clipped).
- **jt81** (CODE 6+0x6a10) — stone window chrome for menu/Hall screens (binds
  "gen" backdrop, FRAME edges, GEN backdrop). Seeds the clip full-screen if zero.
- **jt108(a)** (CODE 6+0x38d0) — hide-cursor / paint-prep (`jt1146/jt1153`).
- **load_menu_ui** (port, `boot.c`) — FSOpen MENU.CTL/FRAME.CTL/GEN.CTL, load
  the base 256-palette (MENU item 0) + FRAME greys into CLUT 16..31 + GEN stone
  backdrop, install via `qd_set_palette`. Sets `g_frame_base`.

---

## Character generation (CODE 17)

- **jt574(ctx)** (CODE 17) — char-gen entry. Drives `l3666` pick → name → body
  icon → `jt573` review → proficiency → finalize.
- **l3666** (CODE 17+0x3666) — the PICK screen: `l35f8` (chrome+headers), then
  `jt452` builds (race/class/alignment/gender radio rows + DONE/EXIT buttons),
  `jt449`, `jt453` modal. DONE button: action `jt572`; EXIT: `jt571`. Buttons are
  shape-1 with `ss=0`, `rec[24]=4` (bevel width) — beveled by `jt137`.
- **l35f8** (CODE 17+0x35f8) — draws `jt76()` then the 4 PICK headers via
  `jt1089` (RACE/ALIGNMENT/GENDER left column h=8006, CLASS right column h=8068).
- **Pick handlers (authoritative for record offsets):** jt566 race→rec[88],
  jt567 gender→rec[92], jt568 class→rec[89]. See `docs/char-record-layout.md`.
- **jt557** (CODE 17+0x6cd2) — TRAIN screen (not "create"; the jt918 case-3
  label is misleading).
- **jt573 / L1346** (CODE 17) — body-icon 7×7 review grid.

---

## `.CTL` art-file item maps (in `data/work/gamedata`, Mac == DOS byte-identical
where checked)

- **ALWAYS.CTL** (group 0) — cursors + markers, NOT button plates. #1 sword,
  #4–11 dir arrows, #12–15 small cross/down-arrow glyphs (**#14 = the down-arrow
  jt382 ss=0 blits — correctly suppressed**), **#16–20 = radio disc markers**,
  #21–24 turn arrows, #26 hourglass.
- **FRAME.CTL** (group 1) — window frame + button bevels. #1 top bar (320×8,
  mode-2), #2/3 left/right edges (8×184, mode-0), **#4 bottom stone molding
  (320×16 @ y=184, mode-2)**, **#10–12 = button bevel left/mid/right (normal),
  #13–15 = highlighted** (11×22 each).
- **MENU.CTL** — #0 base 256-palette, **#1 = command-bar plate (320×16, 3 raised
  bevel segments)**, #2 recessed variant. Used by the main-menu command bar (the
  port's `port_menu_bar` stand-in slices item 1; the *real* per-button bevel is
  `jt137`).

---

## Key A5 globals (UI/dialog)

| slot | meaning |
|------|---------|
| -9254 | DLItem pool base ptr | 
| -9250 | DLItem count |
| -9286 | seeded pool base (copied to -9254 on dialog open) |
| -9288 | DLItem capacity |
| -9282 | 7-entry shape-handler table (method ptrs) |
| -9248 | dialog manager active flag |
| -9247 | one-shot "drawn" flag |
| -12918 | saved previous shape-1 handler (jt151 chain) |
| -3054/-3050/-3056/-3052 | clip rect top/bottom/left/right |
| -2347 | jt1135 scale select (0 ⇒ ×3, else ×2) |
| -7000 | char-gen row colour (135 shallow / 15 deep) |
| -7016 | char-gen header text colour |

---

## How to extend this file

When you reverse-engineer or re-confirm a function:
1. Add it under the right subsystem with: port name, `CODE N + 0xXXXX`, one-line
   purpose, and any **trap** (ABI quirk, off-by-one, port deviation).
2. If it's a method/handler installed indirectly, note *who installs it and when*
   (the jt137/jt151/l4d98 chain is the cautionary tale — the handler existed but
   wasn't installed on the harness path).
3. Cross-link the relevant `docs/*.md` (char-record-layout, decompilation,
   glib-palette-subsystem, etc.).
