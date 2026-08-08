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

- dumps the **cookie jar** — `NOVA` / `EdDI` / `NVDI` / `fVDI` prove the card +
  driver are present, and `_VDO` will still read STE (which is exactly why
  `dsp_detect` can't use it to spot a card);
- records the **framebuffer base** — `Physbase()` / `Logbase()` / `_v_bas_ad`
  (the address the fast path writes chunky pixels to, once the card is the
  active screen);
- opens a VDI screen workstation (AES `graf_handle` → `v_opnvwk`, the documented
  path) and dumps the caps — **`work_out[13]` = #colours**, and from
  `vq_extnd(mode 1)` **`work_out[4]` = planes**, `[5]` = LUT flag. `planes == 8`
  and `colours == 256` confirm an 8bpp paletted screen we can render into.

The probe is **incremental** — it flushes `NOVA.LOG` after each phase, so if a
VDI call ever blocks the machine, the last line names the step and the cookie
jar + base are already on disk. On a plain ST/STe (no card cookie) it records
cookies + base and **skips the VDI open** (which would block with no active VDI
screen) — a useful control run.

Verified on an emulated STe (no card): the AES/`v_opnvwk` path returns handle 2,
320×200, 16 colours, planes 4 (2⁴ = 16 paletted) — i.e. the code is correct; on
the card those become planes 8 / 256 colours.

Send me `NOVA.LOG` and I fill in the four `TODO(NOVA.LOG)` spots in the backend.

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
