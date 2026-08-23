---
name: hardware-media-kit
description: Build and ship real-hardware media — Gotek/FlashFloppy disk images, data disks, the three installers (INSTDISK GEM/TTP, Amiga Install icon, instdisk), USB stick refresh. Use when making .ST/.ADF images, installers, releases for real Atari/Amiga machines, or refreshing the Gotek sticks.
---

Everything between "the build works in the emulator" and "it installs and runs
on the real machine". Scripts: `tools/mkhwdist.sh <version>` (ENGINE images —
redistributable, attach to releases), `tools/mkdatadisks.sh <atari|atari720|
amiga|gotek>` (DATA images — copyrighted, NEVER on GitHub).

## Images and geometry

- FlashFloppy Gotek breaks real-floppy limits by slowing ROTATION, not raising
  the data rate: 1.44 MB at 150 rpm on a stock ST, and the whole 9.4 MB data
  set on ONE 255-cylinder image at 75 rpm (`gotek` mode). Geometry stanzas in
  the generated `IMG.CFG`; Hatari cannot test 255-cyl images.
- Pack by BLOCKS not bytes (FS overhead per file); disk 1's budget minus the
  installer; Amiga FFS counts file-header + extension blocks
  (`mkdatadisks.sh` has the measured math).
- Verify media by EXTRACTING the binaries back out (`mcopy -n`,
  `tools/.venv/bin/xdftool` — not on PATH) and `cmp` against the release zips.
  A zero from an integrity check is only meaningful if the tool actually ran.
- ART policy: every target ships DOS `.tlb`; the engine converts each library
  silently on first touch, once ever (ADR-0019 re-enabled this on the Amiga —
  ADR-0015's hang died with the big-stack overhaul). `uaconv` ships on the
  engine disks as an OPTIONAL bulk converter / space reclaimer (`-d` deletes
  the `.tlb`, ~5 MB). Saves: only the authentic HEIRS Save A ships
  (`SAVE/SAVGAMA.CSV` + `VAULTA.DAT`).

## The three install routes (all end with engine + data in one directory)

1. **Atari `INSTDISK.PRG`** (GEM: file selector, progress bar, swap alerts;
   `.TTP` for the console) — rides on data disk 1. GEM traps: `fsel_exinput`
   VALIDATES its seed path (mkdir it first); the VT52 console paints printf
   over the desktop (silence in GUI mode).
2. **Amiga Workbench `Install` icon** (engine disk) — runs the AmigaOS
   Installer from `SYS:System`/`SYS:Utilities` if present. **WB 3.1 never
   shipped Installer** and its licence (like InstallerNG's) demands a signed
   paper agreement to bundle — so absent an Installer the icon falls back to
   our `instdisk` in the icon's CON: window. Script-language traps: `dest` is
   reserved; `(all)`/`(pattern)` exclusive; alternation patterns silently
   match nothing; a bare `.KEY` line = "Illegal Key directive" on 3.1.
3. **`instdisk`** (portable C, data disk 1): manifest-driven (DISK.LST /
   ENGINE.LST, `a`=append joins the ECS engine halves), reads the DRIVE not
   the volume on Amiga (DosList device walk — the A1200 disk-2 loop),
   auto-detects swaps with `pr_WindowPtr = -1` (else AmigaDOS requesters nag
   per poll), RETURN accepts the default destination, and writes the drawer icon
   (embedded `installer/drawer_icon.h`). No conversion step: the engine
   converts on first touch (ADR-0019).

## Sticks and releases

- Amiga stick: label `Amiga`, `O/OpenUA/` flat layout. Atari stick: label
  `STORE N GO` — the user's 849-image floppy library; OpenUA lives in
  `05_GAMES/OPENUA/` in the COC3D-subfolder style (CRLF README + INDEX.TXT
  entry); its root IMG.CFG already has our geometries. Always `cmp` read-back,
  `sync`, `udisksctl unmount` before saying "safe to pull".
- Releases: `make release-all VERSION=x.y.z-beta` from main (FETCH FIRST),
  `gh release create --prerelease` (NEVER `--latest` — all betas prerelease,
  `/releases/latest` 404s by design), engine `.st`/`.adf` images attach
  BESIDE the zips (the Install icon + ENGINE.LST live in them), no AI footers,
  bump the Makefile `VERSION ?=` default. Integrity: the 68000/020 opcode
  check is a RATIO (~5 vs ~2400 Atari, ~5 vs ~750 Amiga), never a zero test.
  Full recipes + traps: `release-process` memory and `CLAUDE.md`.
