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

## Real card (Mega STe ATW800/2, FPGA "V0205 build 20251025", 2026-08-25)

First hardware run of the diag, diffed against the baseline:

```
seurat: info ascii = V0205 build 20251025 (c) cpm 202
seurat: vtg+00 = 0002 x8            (VTG reads are NOT the registers)
seurat: blit idle+00 = 0002 x8      (register window is write-only-ish)
seurat: cpld version reg = 65530    (= 0xFFFA: high byte floats, low = 0xFA?)
seurat: regs after write = 0000 x9  (readback does NOT return written values)
seurat: status right after go = 0   (so v0.9.17's poll always saw "done")
seurat: copy dst bytes correct (of 64) = 1     <-- cmd 0x0003 moved ONE byte
seurat: copy dst head = 11 00 00 00 00 00 00 00
seurat: fill poll iterations = 0
seurat: fill dst bytes correct (of 128) = 128  <-- cmd 0x0005 fill is CORRECT
seurat: fill dst head = 55 55 55 55 55 55 55 55
```

Conclusions so far:

- The `" cpm"` identity longword matches on BOTH generations — on V0205 it
  is the `(c) cpm` of the copyright string; the layout pins it at +24
  either way, which is why xVDI's own compare is portable. The gate holds.
- The register window does not read back (0x0000 after programming,
  0x0002 later) — completion CANNOT be detected by polling the command
  word on this firmware. Blind-fire only worked by accident.
- cmd 0x0005 fill: byte-perfect with our register model — the init-clear
  half of the blit model is CORRECT on real hardware.
- cmd 0x0003 with (w=64, rows=1): exactly one byte moved, at the correct
  src/dst — the width/row registers (or the command) mean something else
  on V0205. The driver generation matched to this firmware is the
  Sep 2025 xVDI; the model here was traced from the Jul 2026 xVDI.

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

## Second real-card run (2026-08-25 night, xVDI 20260730 in AUTO)

Identical results byte-for-byte: fill 128/128 correct, copy moved
exactly ONE byte (`11` at dst head), every register reads 0x0000 after
write, status never asserts. The installed xVDI generation does not
change any of it — the anomaly is V0205 FPGA behaviour, not a
driver-side programming difference. Any future blit model must satisfy
the same discriminator (fill-perfect + copy-one-byte) regardless of
which xVDI is resident.
