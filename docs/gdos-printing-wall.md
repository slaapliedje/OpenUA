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

1. ~~**VDI trap glue**~~ — **DONE** (`platform/vdi.c` + `plat_vdi.h`): trap #2
   glue with the contrl/intin/ptsin block; `v_opnwk`/`v_clswk`/`v_clrwk`/
   `v_updwk`/`v_gtext`/`vst_font`/`vst_point`, plus `vst_load_fonts`,
   `vqt_name`, `vm_filename` and the d0=-2 GDOS-present probe.
2. ~~**Smoke test**~~ — **META VERIFIED / FX80 OPEN** (the `FRUA_VDIPRINT_TEST`
   hook in src/main.c, runs at main() entry before display init):
   - **GDOS probe: positive. Metafile: PROVEN end to end** — v_opnwk(31) →
     vm_filename → v_gtext → v_updwk → v_clswk produced `C:\PRNTTST.GEM` on the
     host; decoding its 16-bit LE chars shows the attribute records and the
     literal string `FRUA GDOS VDI SMOKE TEST`. This is the byte-diffable
     verification channel for the compat face.
   - **`vst_load_fonts` is MANDATORY**: without it the FX80 workstation has ZERO
     faces (FX80.SYS has no built-in font) and text rasterizes to a silent
     blank page — a well-formed ESC/P page of pure line-feeds (950 bytes of
     `ESC 3` spacing ops) emits, proving v_updwk + Hatari's `--printer`
     redirect work. With the call, the 5 staged Speedo faces attach and
     vst_font(5003) (Swiss 721) selects.
   - **OPEN: the FX80 v_updwk WEDGES when real glyphs must rasterize** (7+ min,
     no output bytes, no Speedo ERROR.TXT; screen shows garbage bands). Not the
     VIDEL mode switch — reproduces before display init. Suspects, in order:
     the small-cache EXTEND preset (BITCACHE=32000) thrashing at 1020×1584;
     try the LARGE preset (`EXTEND.2`), a different driver (NX1000/NECP), a
     much longer wait under fast-forward, or point-size 10 vs 12.
3. ~~**Printing Manager face**~~ — **DONE + SMOKE-VERIFIED** (`compat/printing.c`
   + `printing.h`). The faithful Mac model: `PrOpenDoc` returns a REAL GrafPort;
   the engine SetPorts to it and ordinary QuickDraw text lands on the page.
   `DrawChar` (compat/quickdraw.c) routes to `pr_port_capture` whenever the
   printing port is current, folding jt433's per-char GetPen/DrawChar/MoveTo
   stream into ONE `v_gtext` per line run (run-continuation keyed on "the pen
   sits where the last capture left it"). PrOpen probes GDOS (PrError -192
   absent); the dialogs confirm fixed setup (ADR-0006); PrOpenDoc = v_opnwk +
   vst_load_fonts + the Monospace face at 7pt (+ vm_filename C:\FRUAPRN.GEM on
   the metafile device); pages = v_clrwk/v_updwk; PrCloseDoc = v_clswk. GetFNum
   (synthetic stable ids, compat/quickdraw.c) covers jt428's "Moebius" lookup.
   VERIFIED: the FRUA_VDIPRINT_TEST hook drives the exact jt433 call shape
   through the face — FRUAPRN.GEM lands on the host with BOTH text runs at
   their pen coords ((50,100) + (50,112)), one v_gtext record per line.
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
