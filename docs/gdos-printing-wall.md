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
4. ~~**Un-park the print chain**~~ — **DONE + VERIFIED END TO END.** `jt428`
   (full lift: GetFNum/NewPtr(120)/PrOpen/PrValidate/dialogs/PrOpenDoc/SetPort/
   L4806), `L4806` (PrOpenPage + MoveTo/TextFont/TextSize), the new `L4854`
   (PrClosePage), `jt434` (full lift: L4854 + PrCloseDoc + PrClose + DisposePtr
   + SetPort restore), and the pagination chain (`jt1075`/`jt1072`/`jt1074`)
   un-parked. **The design editor's PRINT command (L541c — what jt254 /
   l0096 case 17 runs) now prints the real design.**

   ⚠ **`jt426`/`jt432`/`jt458` are NOT printing** — they are the Mac
   indexed-catalog file enumeration (`jt990`/`jt991` callers), SUPERSEDED by
   the GEMDOS Fsfirst/Fsnext shim and correctly dead. They stay that way; the
   scoreboard does not go to 1205/1205 from this track.

   **TWO REAL BUGS the un-parking exposed** (both invisible while nothing
   printed):
   - **`jt433`'s FORM FEED was missing its page close.** The Mac arm is TWO
     jsrs — `0x49c4: jsr L4854` (PrClosePage) then `0x49c8: jsr L4806`
     (PrOpenPage) — but `tools/dis68k.py` mis-split the pair (it renders the
     first as a stray `.short 0xfe8e`), so the port had only the open. Pages
     were started and never emitted. The raw bytes settle it:
     `4e ba fe 8e` = `jsr pc@(-370)` → `0x49c6 - 0x172 = 0x4854`.
     **Lesson: when a lift looks odd, read the BYTES, not the disassembler.**
   - **The THINK C `%(X%)` FILL directive was unimplemented.** FRUA's print
     formats use it — the rulers are literally `"%(-%)"` with a width arg (40
     dashes) and the page header is `" %s%( %)Page %2d"` with args (title,
     71-len(title), page). Plain `vsprintf` copied `%(` out literally AND did
     not consume the fill count, so every later conversion read the wrong
     argument: the first run printed **"Page 57"**, and 57 is exactly
     `71 - strlen("OVERLAND 01 - ")`. Fixed with `ua_vsprintf_fill` (boot.c),
     now used by `l7ab4` and `jt1071`.
5. **VERIFIED** (`make EXTRA_CFLAGS="-DFRUA_PRINTTEST=<level>"`, which loads
   that GEO level and runs L541c). HEIRS level 5 → **385 lines / 15 KB** into
   `C:\FRUAPRN.GEM`: the title block with 40-dash rulers, the page header
   right-aligned to "Page  1", the column-number ruler, and the event map as a
   full ASCII floor plan — `+--+` walls, `|` partitions, event indices in their
   cells. Level 1 (an OVERLAND map) prints the same structure with blank wall
   glyphs, which is correct — overland has no walls. `docs/toolbox-mapping.md`
   carries the Printing Manager row.

## Step 6 — the FX80 "wedge": three bugs, none of them a wedge

`PR_VDI_DEVICE=21` (FX80.SYS on the parallel port) is now the **default** and
prints for real: the engine's own chain lands **32 KB of Epson ESC/P** on the
port, and decoding it back to a bitmap shows the HEIRS `OVERLAND 01 - TOWN AT
BEGIN` floor plan — header, 1..18 column ruler, numbered room grid.

It looked like a hang in `vst_load_fonts`. It was three separate faults, and
each one disguised the next:

1. **GDOS `Dsetpath`s into `C:\GEMSYS` and does not put the path back.**
   `v_opnwk` (driver load) and `vst_load_fonts` (face load) both do it. Every
   relative GEMDOS open the engine makes afterwards then resolves under
   `GEMSYS\` and fails — `frua.rsc not found`, `No GEMDOS dir ...\GEMSYS\heirs.dsn`
   — so the app silently ran with **no resources and no design**.
   `platform/vdi.c` now brackets both calls with a drive+path save/restore.

   ★ This is also what made it look like a hang: `dbg_log`/`dbg_file_num` write
   `DBG.LOG` **relative to the current directory**. GDOS moved the CWD, the log
   file stopped being written mid-job, and the missing lines read as "the call
   never returned". **The code was fine; the LOGGING died.** When a trail stops
   dead, ask whether the logger still works before concluding the code hung.

2. **SpeedoGDOS BUS ERRORS inside `vqt_name`** on a bitmap printer workstation
   (reads `$ffffe001`, `PC=$ff4e`). The face-picking loop enumerated names to
   find "Monospace ..." and walked straight into it. Because the engine installs
   `$_exception_handler` and **limps on after a bus error**, there was no bomb —
   the job just printed nothing, which is exactly what a wedge looks like.
   `PrOpenDoc` no longer enumerates: `vst_font(handle, 1)` returns the id it
   actually selected, which is all the shim needs.

3. **Hatari's printer emulation was switched off.** `~/.config/hatari/hatari.cfg`
   had `[Printer] bEnablePrinting = FALSE`, and with it off TOS reports the
   printer busy (`Bcostat(0) == 0`) and never drives the port — so *nothing*
   printed, not even a raw `Bconout(0, 'X')`. Set it `TRUE` (or pass
   `--printer <file>`, which enables it and picks the filename; neither
   `tools/hatari_ui.sh` nor the skill driver passes it, so the **cfg is the
   switch**). Output then lands in `~/.config/hatari/hatari.prn` as a raw byte
   dump of the parallel port — Hatari has **no** print-to-PDF; decode the ESC/P
   yourself to look at a page.

   Hatari feeds that file *only* from PSG writes (`psg.c`: port B = data, port A
   bit 5 = STROBE, transferring on a high->low edge). A raw `Bconout(0, c)` and a
   hand-rolled `Giaccess` strobe bit-bang are both good ways to prove the port is
   alive without involving GDOS at all.

## Remaining on this track

Nothing — both follow-ups closed 2026-07-13:

- jt428 left `jt_progress.py`'s NOOP whitelist ("no print backend" is no
  longer true); it now classifies LIFTED from its real body like any lift.
- `tools/dis68k.py` grew `resync_stream`: the decode restarts after every
  JT[3] inline table, so a garbage "instruction" straddling a table's end can
  no longer eat the first real code bytes after it. The jt433 form-feed site
  now lists `jsr L4854` / `jsr L4806` correctly — and the same straddle had
  garbled the first instruction of case arms at **75 sites** across the fork
  (all now clean; same 307 tables decoded).

## Layering rule reminder

VDI calls live in `platform/` ONLY (the layer that may know the machine); the
compat face translates Mac Printing Manager spellings; engine code calls the
Mac names. Never a `trap #2` in `src/engine/` or `compat/`.
