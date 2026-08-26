# Seurat hardware diagnostic (`novadiag=on`)

The ATW800/2 blitter-present register model was derived from tracing xVDI
against the hatari-et4000 fork's emulation of the card — and the first run
on the real Mega STe ATW800/2 (v0.9.17) failed: full presents through the
2D engine drew garbage while CPU rect presents were fine. The model needs
validating against the REAL FPGA, and this dump is the instrument.

## Running it

Put this in `VIDEO.CFG` next to `FRUA.PRG` and boot once:

```
novadiag=on
```

Everything is appended to `DBG.LOG` in the same directory (file-only —
no console needed). The self-test blits touch only offscreen VRAM past
the visible screen; worst case is garbage in scratch memory, never a
trashed display. `novablit` stays off throughout (it is opt-in now);
the diag drives the 2D engine directly and independently.

## What it dumps

- **info block** (LUT+0x200, 32 bytes hex + ASCII) — the FPGA's version
  string and the `" cpm"` ID longword at +24. The ID is now also the
  IDENTITY GATE for the direct-LUT bind and blit arming: the xVDI cookie
  alone is not enough, since xVDI also drives ET4000-family cards
  (VMG-4000) where these addresses are plain framebuffer RAM.
- **VTG registers** (LUT+0x800, 16 words) — the live mode: ctrl, timings,
  PLL, memory size.
- **blitter registers** (LUT+0x900, 16 words) — idle snapshot.
- **CPLD version word** (0xDFFA98, MegaSTE only, `_MCH`-guarded).
- **Self-test**, each step logged:
  1. CPU pattern write + readback in scratch VRAM (sanity).
  2. Op registers written, then READ BACK before GO — a shifted,
     word-swapped or write-only register window shows up right here.
  3. cmd 0x0003 copy, 1 row: status immediately after GO, poll count,
     final status, registers after, and a byte-count + hex head of the
     destination row.
  4. cmd 0x0005 fill (sstride=0), 2 rows: same logging.

## Emulation baseline (hatari-et4000 `--vme atw800`, 2026-08-25)

The real card's log is diagnosed by DIFFING against this. On the
emulation every step passes; the interesting lines were:

```
nova: fpga id longword = 543387757          (= 0x2063706D, " cpm")
seurat: info ascii = Seurat v0106 Hatari..... cpm....
seurat: vtg+00 = 001D 0010 0060 0030 0280 000C 0002 0023
seurat: vtg+10 = 0190 0017 0004 0003 0000 0000 0000 0000
seurat: cpld version reg = 4
seurat: scratch cpu readback ok = 1
seurat: regs after write = 0003 FC00 0003 FE80 0280 0280 0040 0001 0000
seurat: status right after go = 0           (emulation completes sync)
seurat: copy poll iterations = 0
seurat: copy final status = 0
seurat: copy dst bytes correct (of 64) = 64
seurat: copy dst head = 11 22 33 44 55 66 77 88
seurat: fill final status = 0
seurat: fill dst bytes correct (of 128) = 128
```

Readings on the real card:

- `regs after write` differing from what was written → the register
  window layout/width is wrong (that alone would explain v0.9.17).
- `status right after go` never busy AND dst bytes 0 → the GO word or
  command values are wrong.
- poll timeout (500000) → completion is not "word reads 0".
- dst bytes partially right → stride/width units are wrong (e.g. words
  not bytes, or a per-mode pitch that is not 640).
- info ascii shows the real FPGA version — behaviour may differ from
  the v0106 the manual describes.
