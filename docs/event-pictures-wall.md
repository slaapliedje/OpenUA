# Event-pictures / portrait subsystem — findings + worklist

Goal: the illustrated pictures attached to dungeon events (merchant, tavern,
intro caravan, monster portraits) render correctly, including the **intro caravan
at dungeon entry** (currently a black frame) and **without corrupting the 3D view
on walk-back**. The runtime pipeline is **faithful and works** for the
tavern/shop/temple/question events — the two remaining issues are *composition
ordering* and *buffer sharing*, **not** the palette math.

> CORRECTION to the roadmap: **CODE 10 is the picture EDITOR/importer
> (design-tool side), not the runtime display path.** Its entries own the
> resource-name formats (`"PIC%c1%03d.tlb"`, `"SPRI0%03d.tlb"`, `"CPIC1%03d.tlb"`,
> `"BIGP0%03d.tlb"`) + MacPaint/PICT import. The runtime painter is `l442e`
> (CODE 20); the bigpic load/palette commit live in CODE 6 / CODE 5. **No runtime
> picture display depends on CODE 10** — those 8 missing entries are deferred
> design-tools (ADR-0008).

## The picture-present pipeline (faithful, traced end-to-end)

1. **Dispatch** — `l709e` (boot.c:3296) gates on `l694e` (validity + once-only),
   sets the fired-bit `l4336`, dispatches `ev[0]` (7=tavern `l4f9a`, 8=shop
   `l5586`, 9=temple `l216a`, 36=question `l3118`).
2. **Painter** — the handler calls `l442e(ev)` iff `ev[6]` (picture id) is set.
3. **Branch** (`l442e`, boot.c:32460):
   - **id ≥ 240 (bigpic backdrop):** `l579e(ev[6])` load → `jt44`/`l5822` blit.
   - **id < 240 (sprite/PIC marker):** sets slot bytes, computes facing index
     `rec[56]`, depth-probes `l1476`→`rec[55]`, composites via `l08ce`.
4. **Load** — `l579e` (boot.c:48586): cache-check `-24256`, `jt488` builds
   `"bigpi%c%d"`, `l33ac` binds the GLIB group into `-24260`, then
   **`l3f3c(32,255)`** installs the palette. (PIC/SPRIT loader = `l541a`, 48686.)
5. **Decode** — GLIB codecs: type 2 PackBits `jt1171` (1286), type 7 transparency
   RLE inlined `jt1195` (19672), type 9 composite. Faithful.
6. **Blit** — `l5822`→`l3880(1,1,…)`→`jt1001` at 8000-origin → `l3eea` commit.
7. **Palette commit** — `l3eea` (48489): `jt468`→`jt1017`→**`jt993`** (read type-8
   palette block) → `jt1069` allocate → `jt1066` promote → `l6e58` →
   `qd_set_palette` (the one hardware CLUT write).

The load → decode → palette-commit → blit chain is faithful 1:1 and
Hatari-verified for tavern/shop/temple/question. The remaining stand-in is the
**HUD composer** `port_draw_play_frame` (boot.c:11312), a coarse FRAME.CTL
over-blit replacing the faithful `jt304`/`L3fd8` — separate from the picture path.

## The palette model (DO NOT change the math)

- **Range split:** pictures own CLUT **32..255**; `l579e`/`l541a` both install
  over `l3f3c(32,255)`. UI band 0..31 is meant to be reserved.
- **Hardware guard:** `l6e58` (46781) refuses indices < 16 (Mac reserves 0..15);
  each 8-bit channel widened to `(b<<8)|b`.
- **3-stage install:** `jt1069` stages RGB into the LIVE buffer `-3390` + marks
  the used-bitmap; `jt1066` promotes live→work `-3394`, tracks `[min,max]`, pushes
  that span via `l6e58`; `jt993` derives the window — explicit when `hdr[1]&1`,
  else `0..256` or `0..16` by display mode.
- **The clobber:** `jt993` (47097) sets a **port-added** `g_clut_clobbered` flag
  whenever a picture lands below index 145 (every event picture). That flag tells
  the dungeon renderer (11289) to force-reload the wall CLUTs + backdrop band +
  chrome on the **next** frame. The historical "black picture" bug was a reversed
  `jt406(live,work)` in `jt1066` (now fixed, documented @46964).

> The 0..31 split, `l6e58`'s `<16` guard, and `jt993`'s window derivation are all
> faithful to the Mac. Per the project CLUT-sensitivity rule, **only composition
> ordering and buffer sharing should change — never the palette ranges/math.**

## The two known bugs

### Bug A — viewport bevel + compass BLACK at a landing-cell event — FIXED (fe9a42d)
**RESOLVED 2026-06-21.** Root cause (measured): the event picture's GLIB palette
commit blacks out **CLUT 16..31** (the FRAME.CTL chrome band) even though `jt993`
reports the picture's declared range as start=32 — the `jt1069`/`jt1066`
allocator/commit reaches below 32. Proof via surface probe: the bevel/compass
*pixels* stay on `g_surface` (sampled values 30/31, in the frame band) while
their CLUT entries go black; the panels (value 8) are fine. So it was a palette
clobber of the upper frame band — NOT buffer coherency or draw order (both
disproven). The VBL triple-buffer is innocent: `present()` blits the full
`g_surface` every frame.

**Fix:** `port_reinstall_frame_band()` re-pushes the FRAME.CTL set-0 palette into
CLUT 16..31 at `l442e`'s tail (dungeon mode only) — the same band reinstall
`port_draw_play_frame` already does every normal frame, now applied after the
event picture. Touches no pixels, not the picture's 32..255, not text (112..143).
Hatari-verified: the HEIRS merchant event shows the full stone frame + compass
around the portrait. The detail below is kept as the investigation record.

#### (original investigation — superseded by the fix above)
Symptom (HEIRS opening caravan = the case-8 SHOP event, `l5586`→`l442e`): the
merchant portrait, HUD roster, position/clock, content-window text + Return all
render correctly, but the **88×88 viewport bevel (FRAME.CTL piece 9) + compass
ring (piece 21)** read BLACK — only the compass *face* marks survive, "floating."
Long-standing (since the event launcher).

**MEASURED 2026-06-21 (instrumented `jt993`/compose-order probes, Hatari):**
- **NOT a palette clobber.** The merchant picture installs its palette at
  **CLUT 32..255** (`DBGjt993 start=32 count=224`) — the wall/backdrop bands, NOT
  the frame band (0..31). Every earlier "0..16 clobber" / `g_clut_clobbered`
  theory above is **dead**. Do not chase the palette.
- **The chrome IS composed at entry.** Entry compose order (probed):
  `jt23(mode4)` → `jt935` takes the **L003e** branch (NOT first-entry) → `jt221`
  → `render_3d_faithful` → `port_draw_play_frame` (**`DBGpdpf` fires** — bevel +
  compass + grey drawn) → 2 more `jt935`/`render_3d_faithful` (3D walls) →
  **`l442e`** (the event picture) LAST.
- So the bevel/compass ARE drawn (by `pdpf`), THEN the event paints. Yet the
  presented frame lacks them while it DOES keep the event's own panels (HUD box,
  content window) — i.e. **part** of the chrome is missing and part isn't.

**Leading hypothesis: VBL triple-buffer incoherency.** `port_draw_play_frame`
(bevel/compass) runs in `render_3d_faithful` on one work buffer; the event's
draws (`l442e` portrait via `l534a`, `jt937` HUD, `jt938`, the text box) land on
the buffer that ends up displayed — which never received the viewport bevel +
compass. The panels survive because the EVENT draws them; the bevel/compass don't
because only `pdpf` (earlier, other buffer) did.

**Approaches TRIED + REVERTED this session (all clean now, none shipped):**
1. Compose chrome in `jt935` first-entry branch — **dead code**: jt935 takes the
   L003e branch at entry, never first-entry (probed).
2. Re-lay frame pieces over the picture (`l67ca`-style after `l08ce`) — **covered
   the portrait**: FRAME piece 9 has a SOLID interior (drawn first, then the view
   fills the hole), so drawing it after the picture blacks the portrait.
3. Clip the event picture to the 88×88 hole via the QD clip globals
   (`g_a5_3056/3054/3052/3050`, which `l2d4e` honours) — set before `l08ce` (reset
   by `l541a`) AND inside `l534a` after `jt108` — **no visible effect**, which
   itself argues the portrait is NOT overrunning and the bug is buffer coherency,
   not a clip.

**NEXT (focused task): make the event present buffer-coherent.** Compose the full
play frame (chrome: bevel + compass) on the SAME work buffer the event paints,
at the START of the event present (before the portrait), so the displayed frame
has chrome + picture + panels together. Needs care with the triple-buffer present
semantics ([[vbl-cursor-service]] / display HAL). Confirm with the same
`DBGpdpf`/buffer probes. Do NOT touch the picture palette (proven irrelevant).

### Bug B — walk-back re-fire corrupts the 3D view
The once-only mechanism **is** present: `l694e` gate C (3122) suppresses re-fire
when `ev[1]&0x01` and the fired-bit (`rec[525 + bit/8]`, bit `level*100+idx-1`) is
set. So re-fire only happens for **repeatable** events (no once-only flag). For
those, walking back re-runs `l442e`, and the picture load (`l579e`/`l541a`)
**stomps the shared `g_wallfile_buf`** — the GLIB handles stay valid but the item-0
palette is zeroed, so `cw_load_slot` extracts an all-black band and walls go
invisible (documented @11291). `g_clut_clobbered` forces a fresh wall re-read to
recover. **Faithful fix touches:** give the event-picture loader its own GLIB
buffer (the Mac keeps wall art and picture art as *separate* purgeable RM handles;
the port shares one static buffer as a memory-saving stand-in — see
[[dungeon-walls-4mb-fix]]).

## Status table

| Fn | addr | Status | boot.c | Role |
|----|------|--------|--------|------|
| `l442e` | CODE20+0x442e | **LIFTED** | 32449 | Event painter: bigpic (id≥240) vs sprite/PIC marker (id<240) |
| `l08ce` | CODE20+0x08ce | **LIFTED** | 48820 | 2-layer sprite/PIC composite |
| `l579e` (JT[43]) | CODE6+0x579e | **LIFTED** | 48586 | Load "bigpi%c%d" backdrop + install palette via l3f3c |
| `l5822` (JT[44]) | CODE6+0x5822 | **LIFTED** | 48607 | Reblit cached bigpic at (1,1) + commit |
| `l541a` | CODE6+0x541a | **LIFTED** | 48686 | Load a PIC/SPRIT/CPIC group + install/commit its palette |
| `jt214` | CODE7+0x71c6 | **LIFTED** | 13320 | Select play-screen bigpic id → l579e |
| `jt23` | CODE6+0x2890 | **LIFTED** | 48879 | Play-frame stand-up; gate→l5822; bigpic DRAW not wired into case-4 |
| `l3eea` (jt124) | CODE6+0x3eea | **LIFTED** | 48489 | GLIB picture+palette commit |
| `l3f3c` (JT[105]) | CODE6+0x3f3c | **LIFTED** | 48565 | Install picture palette range [lo..hi] |
| `jt993` "TNPalette" | CODE5+0x20d0 | **LIFTED** | 47048 | Read type-8 palette, derive CLUT window, jt1069 |
| `jt1069`/`jt1066`/`jt1067`/`jt1068` | CODE5 | **LIFTED** | 46847/46972/47110/46810 | alloc / commit / cycle / init the two 768B RGB buffers |
| `l6e58` | CODE6+0x6e58 | **LIFTED** | 46773 | Hardware CLUT write → qd_set_palette |
| `jt1171` "_UnpackBits" | CODE4+0x108e | **LIFTED** | 1286 | type-2 PackBits decoder |
| `jt1195` transparency RLE | CODE4+0xc08 | **LIFTED** (inlined) | 19672 | type-7 RLE (inlined into the blit) |
| `l4f9a`/`l5586`/`l216a`/`l3118` | CODE20 | **LIFTED** | 33462/53919/54174/32619 | tavern/shop/temple/question — all call l442e |
| `l159a` (case 1) | CODE20 | **STUB** | 3249 | animated event |
| `l1f76`/`l40b4` (case 4) | CODE20 | **STUB** | 3253/3252 | event + sound pre-hook |
| CODE10 `jt259/260/264-270` | CODE10 | **MISSING** | — | picture EDITOR/importer (design-tools, deferred) |

## Plan (highest-leverage first)

1. **Compose-before-picture at dungeon entry (Bug A)** — wire the latent bigpic
   DRAW into `jt23` case-4 (48935), re-order so the UI-band reinstall runs after
   `jt1066`. Highest-leverage + most visible (the black intro caravan). Touches
   `jt23` + the frame ordering in the dungeon render loop (11289). A captured Mac
   entry-frame blit trace ([[mac-blit-ground-truth]]) would pin whether the Mac
   composes before or after the picture — resolve the exact ordering without
   guessing.
2. **Separate the event-picture buffer from the wall buffer (Bug B)** — give
   `l541a`/`l579e` their own GLIB load buffer so a picture load can't zero
   `g_wallfile_buf`'s palette (11291). Removes the walk-back 3D-view corruption.
   Lower risk than touching the CLUT.
3. **DO NOT touch the palette ranges/window logic** — faithful already.
4. **(Later)** the CODE 10 picture-editor entries — design-tools, no runtime
   dependency, defer per ADR-0008.

Related: [[bigpic-composer-129]], [[resource-manager-bigpic-pickup]],
[[glib-palette-subsystem]], [[dungeon-walls-4mb-fix]].
