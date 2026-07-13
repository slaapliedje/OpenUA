# GDOS printing — scope + worklist

Goal: make the Mac **Printing Manager** subsystem real on the Atari — the whole
print chain is lifted but dead (`jt428` open / `jt433` char-emit / `jt434`
close / `L4806` page rollover, plus `jt1075` → `jt256`/`jt1074`/`jt1072` and the
three SUPERSEDED Toolbox entries `jt426`/`jt432`/`jt458`). The faithful Atari
mapping is **GDOS + a VDI printer workstation**, NOT a Mac-shim reimplementation
(see the ratified plan in the `gdos-printing-backend-plan` memory).

## DONE — SpeedoGDOS 5.7 staged and BOOTING (2026-07-12)

The user supplied the five SpeedoGDOS 5.7 distribution floppies at
`data/SPDO57_[1-5].ST`. **`tools/stage_gdos.sh`** hand-installs them into the
Hatari C: mount (no INSTALL.PRG):

```
AUTO\SPEEDO57.PRG + SPDSPD57.PRX + SPDTTF57.PRX    the GDOS + Speedo/TTF engines
GEMSYS\FX80.SYS  META.SYS  MEMORY.SYS              printer / metafile / memory drivers
FONTS\BX00000[3-6].SPD BX000510.SPD + .TDFs        Bitstream base faces + Monospace
ASSIGN.SYS                                         1-9 = screen (resident), 21 = FX80, 31 = META
EXTEND.SYS                                         PATH=C:\FONTS\, STOP=0, small-cache preset
SPDTMP\CHR\                                        Speedo's character cache dir
```

**Verified:** Speedo 5.7a (Oct 6 1997, Bitstream scaler) loads from the
GEMDOS-HD AUTO folder — banner + font scan + ~322K of caches — and `frua.prg`
boots normally on top of it; the FRUA menu renders untouched. Unattended boot
confirmed (the harness needs no changes).

Gotchas baked into the stage script:
- `EXTEND.SYS` ships with **`STOP=1`**, which pauses boot at the banner with
  "Press any key" — fatal for the headless harness. Force `STOP=0`.
- `CACHEFILE=#ECACHE2#` is an installer placeholder (Speedo warns about the
  `#`); strip to a plain name.
- The shipped config templates hide at `ASSIGN\NOFONTS\ASSIGN.SYS` and
  `EXTEND\{SMALL,LARGE}\EXTEND.[12]`, both with a `PATHX` placeholder.

## Worklist (smallest-first)

1. **VDI trap glue** (`platform/`): a minimal VDI binding — `trap #2` with
   d0=0x73, the contrl/intin/ptsin parameter block. Only what printing needs:
   `v_opnwk`, `v_clswk`, `v_clrwk`, `v_updwk`, `v_gtext`, `vst_font`,
   `vst_point`, `vqt_extent`.
2. **Smoke test outside the engine**: open device **31 (META.SYS)** —
   `v_opnwk(31)` → `v_gtext` → `v_clswk` → a `.GEM` metafile appears on C:.
   Byte-diffable on the host; no printer emulation needed. Then device **21
   (FX80)** with Hatari `--printer <file>` for the ESC/P path.
3. **Printing Manager face** (`compat/`): `PrOpen`/`PrOpenDoc` → `v_opnwk(21)`,
   `PrOpenPage`/`PrClosePage` → `v_clrwk`/`v_updwk`, `PrCloseDoc`/`PrClose` →
   `v_clswk`; the `-9152` "Moebius" print font → `vst_font` on the printer
   workstation. Engine code keeps the Mac spellings (ADR-0003).
4. **Un-park the print chain**: flip `jt428`/`jt433`/`jt434`/`L4806` +
   `jt1075`/`jt256`/`jt1074`/`jt1072` from dead/NOOP to live bodies over the
   compat face; lift `jt426`/`jt432`/`jt458` (the last 3 MISSING JT entries).
5. **End-to-end verify**: print a character sheet in-game → read the ESC/P (or
   metafile) off the host disk. Update `docs/toolbox-mapping.md` (new Printing
   Manager row) and the jt_progress NOOP note for jt428.

## Layering rule reminder

VDI calls live in `platform/` ONLY (the layer that may know the machine); the
compat face translates Mac Printing Manager spellings; engine code calls the
Mac names. Never a `trap #2` in `src/engine/` or `compat/`.
