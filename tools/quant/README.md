# Quantiser analysis tools (#139)

Three host programs that measure the ST/STe 16-colour (and ECS 32-colour)
reduction **through the engine's own `quant_banded`** — they `#include`
`platform/include/quantize.h` rather than re-implementing the algorithm, so a
number here is comparable to what the machine renders.

That distinction is why they exist. `tools/quantsim.py` median-cuts an RGB image
directly and models an *idealised* per-pixel dither; it therefore measures a
different pipeline and overstates what is shippable.

```sh
make            # builds qfid, qpre, qdither
```

## Getting real input

Both inputs are what `quant_banded` is about to see: a 320x200 8-bit index
surface and the 768-byte CLUT.

```sh
make CPU68K=68000 EXTRA_CFLAGS=-DFRUA_QDUMP     # from the repo root
# boot as usual; each re-band writes q<NN>.frm + q<NN>.clt into the
# GEMDOS-mounted gamedata dir
tools/quant/qfid 1 16 data/work/gamedata/q03.frm data/work/gamedata/q03.clt
```

`qsrc` is `s_shadow` when a viewport is committed, so a captured walk frame
**includes the 3D view overlaid** — which is what the palette is derived from.

★ **Never commit a capture.** They are frame buffers of copyrighted art; `data/`
is git-ignored for the same reason.

## The tools

| | |
|---|---|
| `qfid <nbands> <ncol> <frm> <clt>` | Fidelity of the shipping path: mean squared error against every pixel's true CLUT colour. |
| `qpre <frm> <clt> [ncol] [nbands]` | A/B/C — shipping cut vs population-weighted vs +Floyd-Steinberg. Answers "how much is offline worth". |
| `qdither <frm> <clt> ...` | Models dither as the **N-LUT** form a backend can afford (`row2`, `check2`, `bayer4`), not a per-pixel search. |

## Reading the numbers

MSE **rises** with any dither — measured 326 vs 252 on the title screen — because
error diffusion trades pointwise accuracy for perceived tone. Judge dithering by
eye; use MSE only to compare non-dithered variants.

The ST currently calls `quant_banded` with **`nbands = 1`** and replicates the one
palette to every band (ADR-0016 B1, to kill the #40 seams), so `nbands > 1`
numbers describe a configuration that is not shipping today.
