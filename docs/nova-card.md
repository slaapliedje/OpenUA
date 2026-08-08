# Graphics-card display backend (Nova / NVDI, e.g. ATW800/2)

OpenUA is a chunky-256-colour engine (the Mac heritage). On the Falcon (VIDEL)
and Amiga RTG that runs **natively, with no conversion**. A Nova/NVDI graphics
card on a Mega ST/STe/TT is the same shape: an **8bpp chunky framebuffer** with
the palette in hardware — the cleanest non-Falcon Atari target, because it
deletes the whole ADR-0016 bitplane/c2p problem that the ST/STe/TT builds carry.

The **ATW800/2** (geekdot.com/atw800_2) is one such card: an FPGA "Seurat" core
with 2 MB VRAM and a **2D blitter (~130 MB/s)**, driven by a branch of the
**Nova** VDI drivers, HDMI out. Only the graphics half matters to us; the
Transputer/FPGA/flash-ROM parts are irrelevant. Because it is Nova/NVDI-
compatible, the backend below targets *any* Nova-class card, not just this one.

Development is **hardware-only** — no emulator here models a Nova/VME card — so
the workflow is: run a probe on the real machine, code the backend from the
numbers it dumps.

## Step 1 — run the probe (`platform/nova_probe.c`)

Build a diagnostic binary and run it on the Mega STe **with the card active as
the boot/desktop screen** (so the Nova driver and its cookies are up):

```sh
make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_NOVAPROBE'
m68k-atari-mint-strip frua.prg          # optional; smaller
# copy frua.prg to the card, boot it once
```

It writes **`NOVA.LOG`** in the game directory and then continues booting
normally. The probe:

- dumps the **cookie jar** — `NOVA` / `EdDI` / `NVDI` / `fVDI` are *hints* a card
  driver is up, but **do not gate on them** (see the hardware finding below);
  `_VDO` still reads STE with a card fitted, which is why `dsp_detect` can't use
  it to spot a card;
- records the **framebuffer base** — `Physbase()` / `Logbase()` / `_v_bas_ad`
  (the address the fast path writes chunky pixels to, once the card is the
  active screen);
- **queries the LIVE screen** — `vq_extnd()` on the AES physical handle from
  `graf_handle()` (the desktop's own workstation = the card), reporting
  **`work_out[13]` = #colours** and **`vq_extnd(mode 1) work_out[4]` = planes**.
  `planes == 8` / `colours == 256` confirm an 8bpp paletted screen. It ALSO
  dumps the `v_opnvwk(Getrez()+2)` device-id path for comparison.

It flushes `NOVA.LOG` after each phase (breadcrumbs), and runs whenever the AES
is up (`appl_init` succeeds); on a plain machine it just reports the ST screen —
a useful control. Verified on an emulated STe: `graf_handle` = 1, 320×200, 16
colours, planes 4 (2⁴ = 16 paletted) — the query is correct; on the card the
live query reports 256 / planes 8.

### ★ Hardware finding (2026-08-08, ATW800/2 + xVDI driver)

- The card driver is **xVDI**; its **lowest resolution is 640×400** (256-colour
  or 16-bit). No 320×200 — but **640×400 is exactly 2× 320×200**, so the backend
  integer-doubles the engine surface (clean, no fractional scaling).
- **Getrez() returns 2 (ST-High) at 640×400.** That poisons two device-id
  paths: (1) the engine's `dsp_detect` did `Getrez()==2 → ST-High mono backend`,
  and (2) a `v_opnvwk(Getrez()+2)` opens the 640×400×**2** ST-compat mode — which
  is exactly the "detected 640×400×2" symptom. The cure everywhere: **ask the
  VDI (via the `graf_handle` physical workstation) for the real depth**, never
  trust `Getrez()`.
- xVDI does **not** set `NOVA`/`EdDI`/`NVDI`/`fVDI` — so an early cookie gate
  wrongly skipped the query. The probe no longer gates on the cookie.

### ★ Confirmed card values (real ATW800/2, `NOVA.LOG` 2026-08-08)

The `graf_handle` live query settled it:

| | value |
|---|---|
| mode | **640×400×256**, `planes=8`, `lut=1` → 8bpp **chunky** |
| framebuffer base | **`$FEA00000`** (`Logbase` = `Physbase` = `_v_bas_ad`) — card VRAM aperture |
| cookies | `EdDI` ($0004C59E), `xVDI` ($0001D4B4) |
| pitch | assumed **640** (= width; unconfirmed vs padded — see below) |

Both the live query and the `v_opnvwk(Getrez()+2)` path agreed on this card
(640×400×256×8), so the only thing that ever reported ST-High was the engine's
own `dsp_detect` (`Getrez()==2`), which the `FRUA_NOVA` hook now sidesteps.

`platform/display_nova.c` is filled in with these: base `Logbase()`, card
640×400, engine 320×240 surface **centred** (black surround) — a first render to
validate base/pitch/format/palette before 2×-scaling to fill the screen. Build
and run it on the card (booted into the xVDI desktop):

```sh
make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_NOVA'   # the card render binary (FRUACARD.PRG)
```

On a non-card machine it detects `planes!=8` and hands back to the ST/STe
backend (verified on an emulated STe — boots normally). **Reading the result:**
a correct FRUA menu centred on screen confirms everything; a **diagonal
shear/repeat** means the pitch is padded (not 640); **wrong colours, right
shapes** means the VDI pen→slot map isn't identity (set the card CLUT directly);
**garbage** would mean it isn't chunky (unlikely — EdDI + the geekdot spec say
packed pixels). The known menu layout makes each failure mode self-diagnosing.

## Step 2 — the backend (`platform/display_nova.c`)

A **provisional scaffold**, gated on `-DFRUA_NOVA`. It is an empty object in
every shipping build and is only selected by `dsp_detect()` (before the ST/STe
bitplane fallback) when that flag is set **and** the screen is confirmed 8bpp —
otherwise it hands back to the ST/STe backend. It sets `hw_palette = 1` (index
== CLUT slot, the TT/AGA identity, so the #99 dirty-row present skip works).

Data-flow (the `display_rtg` model): the engine renders into a chunky surface in
local RAM; `present`/`present_rect` copy to the card aperture. A later pass hands
the big copies to the card's **2D blitter** via accelerated VDI, off the 68000.

What still needs real numbers from `NOVA.LOG` (marked `TODO(NOVA.LOG)`):

1. **Card aperture + row pitch** — `nova_init` assumes `Logbase()` and
   `pitch == width`; confirm both (a padded pitch just needs the real value —
   the present already loops per row).
2. **The mode / resolution** — the card's smallest 256-colour mode is likely
   640×480 or 1024×768, not 320×200. We centre/integer-scale the 320×200 engine
   surface into it (the `display_videl` letterbox path already does this).
3. **Palette** — `vs_color` (0..1000 scaling) is correct via VDI; a direct CLUT
   write is a later speed-up once the register address is known.

Build it (once specs are in) with:

```sh
make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_NOVA'               # card build
make CPU68K=68000 EXTRA_CFLAGS='-DFRUA_NOVA -DFRUA_NOVAPROBE'  # card build + probe
```

## Performance note

Deleting c2p is a real win, and the card's blitter can offload the big copies
(screen clears, viewport composite, bigpic blits), leaving only the 3D-scene
rasterisation 68000-bound — the same cost as every ST-family machine. Writes go
to local RAM then to VRAM; do **not** CPU-write pixels one at a time across the
bus into card VRAM. Whether it beats the planar STe path is a `FRUA_STPROF`
measurement on real iron (#90), not a promise.
