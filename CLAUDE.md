# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A port of SSI's *Forgotten Realms: Unlimited Adventures* (FRUA, 1993) to the
Atari Falcon030 and TT030, based on a decompilation of the **Macintosh 68k**
release. Mac 68k → Atari 68030: the CPU code carries over; the work is
retargeting the Mac Toolbox and the display/sound paths.

## Ratified decisions (see docs/decisions.md before changing course)

- Port from the **Macintosh** release, not MS-DOS (ADR-0001).
- **Hybrid** decompilation: recompilable C, with 68k asm where lifting is hard
  (ADR-0002).
- **Mac Toolbox compatibility shim** first; migrate to native Atari APIs later
  (ADR-0003).
- **m68k-atari-mint GCC** cross toolchain (ADR-0004).
- **Display HAL** with VIDEL / TT-shifter / VDI backends (ADR-0005).
- **Editor UI** reimplemented inside the Toolbox shim, not mapped to AES
  (ADR-0006).
- **Resource fork** delivered as a flat `(type, id)` archive built by
  `tools/rsrcpack` (ADR-0007).
- **Runtime first**: port the play-UA-modules runtime before the design tools
  (ADR-0008).
- **Disassembly** via `tools/dis68k.py` (objdump-driven); listings are
  git-ignored, the lifted C in `src/engine/` is the committed work (ADR-0009).

## Source material

The Mac release ships as a StuffIt of three DiskCopy floppy images. Unpacking
it (StuffIt → HFS → DiskDoubler → DDAR) is documented in `docs/mac-release.md`,
including how to produce `data/work/UnlimitedAdventures.rfork` — the
application resource fork holding the 23 `CODE` segments that are the
decompilation target. Built with THINK C (A5-relative globals; `CREL`/`DREL`
relocations). `data/` is git-ignored; nothing there is committed.

`python3 tools/dis68k.py data/work/UnlimitedAdventures.rfork` regenerates the
annotated disassembly under `data/work/disasm/`. The runtime model and the
lifting workflow are in `docs/decompilation.md`.

## Layer rule

`src/engine/` (engine) → `compat/` (Mac Toolbox shim) → `platform/` (HAL) →
TOS. Only `platform/` may know which machine it runs on. Engine code must not
call XBIOS/TOS directly — go through the shim or the HAL.

## Build / test

```sh
make            # build frua.prg (soft-float — runs on Falcon030 and TT030)
make FPU=1      # FPU-required TT030 variant
make run        # boot in Hatari (Falcon mode)
make test       # host-side pytest suite over tools/ (tests/, synthetic data)
```

## Toolchain flags (non-negotiable)

- `-m68020-60` — runs on 020/030/040/060; gets 32-bit muls/divs and
  bitfield ops. Falcon030 and TT030 are both 68030, so this is the
  correct target. Do NOT use bare `-m68000` or default codegen.
- `-msoft-float` in the default build (Falcon030 has no FPU).
- `make FPU=1` flips to `-m68881` and drops `-msoft-float`; only for
  the TT030 variant.
- `-std=gnu99`, `-fomit-frame-pointer`, `-Wall -Wextra`.
- Verify with: `m68k-atari-mint-objdump -d build/<obj>.o | grep -E 'muls\.l|bfextu|bfins'`
  — if you see none of these across the whole build, the flag isn't taking effect.

If a build error looks fixable by changing flags, fix the Makefile and
show the diff before touching source.

Default build is **soft-float** so one binary serves the FPU-less Falcon030
and the 68882-equipped TT030. Do not assume an FPU in shared code.

## Conventions

- C is `gnu99`; tabs for indentation (matches existing files).
- Keep ratified decisions in `docs/decisions.md` (append-only ADRs).
- Update `docs/toolbox-mapping.md` whenever a Toolbox manager's status changes.
- Never commit anything under `data/` — original FRUA assets are copyrighted.

## Decompilation workflow

### Lift levels — pick one per function

When porting a Mac function across, choose the level that fits the
session's scope; don't promise more than you can sustain:

1. **PROBE-only stub** — `static <ret> jtN(...) { PROBE("jtN"); ... }`.
   Records the call in the engine probe. Right for leaf entries whose
   body lives in CODE we haven't touched, or for entries that don't yet
   gate engine progress.
2. **Structural skeleton** — full C body that mirrors the asm CFG and
   calls every JT entry in order, but inner per-arm dispatch is
   deferred with `/* TODO */` comments. Right when the call sequence
   matters more than the per-arm work (e.g., dialog-loop dispatchers).
3. **Full lift** — every arm faithfully translated; the only stubs
   left are leaf JT entries the function calls. Reserve for tight
   functions or critical paths.

The L02dc / L12a0 / L15e2 commits are good examples of level 3 / 2
trade-offs. Match the depth to the session's budget, not the
function's importance.

### Naming

- `jtNNN` — a JT entry, lifted from `data/work/disasm/jumptable.txt`.
  Always lower-case; the asm uses `JT[NNN]` upper-case.
- `lXXXX` — a CODE-local helper at hex address `0xXXXX` (e.g., `l02dc`).
  Lower-case to match `jtNNN`. Always reach across the prefix when
  cross-segment.
  - **Before lifting any `lXXXX`, check `docs/lxxxx-jt-aliases.md`.** If the
    label is also a `JT[N]` export it is the SAME function and is very likely
    already lifted as `jtN` — alias/repoint to it instead of re-lifting (the
    `l30bc`=`jt882`, `l25ce`=`jt893`, `l2f6e` duplicate traps). The same hex
    offset recurs across CODE segments (`l3540`/`l2f6e`/`l23d2` exist in
    several) — these are DIFFERENT functions; match on `(CODE, offset)` and, on
    a name clash with an already-lifted other-segment helper, suffix `_cNN`.
    Regenerate the map with `tools/gen_jt_aliases.sh > docs/lxxxx-jt-aliases.md`.
- `g_a5_N` — an A5-world global at offset `-N`. Read as a macro over
  `g_a5_below[]` (see below).

### A5-world storage

The A5-relative globals live in a single `g_a5_below[A5_BELOW_SIZE]`
buffer set up by `data_pool_replay()` (zero-fill + DATA blit + DREL
relocs). Address each slot via typed macros at the top of `boot.c`:

```c
#define g_a5_NNNN g_a5_byte(-NNNN)    /* or _word / _long / _ptr / _buf / _chars / _shorts / _longs */
```

The macros work as l-values (assign through them) and rvalues.
Non-zero scalars get re-seeded in `boot_a5_seed_defaults()` since
the buffer is zero on startup.

**Heap-equivalent** buffers (NewPtr / NewHandle on the Mac side)
stay file-static in C — the address ends up in an A5 *pointer* slot,
but the bytes themselves are not part of the A5 world. `g_dlitem_pool`
is the canonical example.

### Toolbox shim (compat/)

Mac Toolbox types and calls live in `compat/mac_compat.h` (+ `.c` for
non-inline bodies). Engine code keeps the Mac spellings — `FSOpen`,
`NewHandle`, `OSErr`, `Str255`, `ConstStr255Param`, `noErr`, `fnfErr`,
etc. — and the shim routes them to GEMDOS / Mxalloc / etc.

Rules:
- New Toolbox symbols go into `mac_compat.h`; never inline a GEMDOS
  call in `src/engine/`.
- Keep returning Mac `OSErr` to callers. Translate GEMDOS errors at
  the shim boundary (`gemdos_to_oserr()`).
- Pascal strings are converted to C strings inside the shim
  (`p2cstr_buf`); call sites stay Pascal.
- File handles: GEMDOS handle fits in `int16_t`, store directly in
  the Mac `refNum` slot. No descriptor table needed.
- Memory: `NewPtr`/`NewHandle` over `malloc`. Use `Mxalloc(size, 0)`
  (ST-RAM) for buffers the Videl/DMA-sound/DSP will touch; flag
  these as `NewPtrST` in the shim and call them explicitly from
  `platform/` — not from engine code.
- When a Toolbox manager's coverage changes, update
  `docs/toolbox-mapping.md` in the same commit.

If a Toolbox call has no clean GEMDOS/HAL mapping, stop and ask —
do NOT stub it to return `noErr`.

### THINK C inline switch (JT[3])

Every `switch` in the Mac build compiles to `jsr JT[3]` followed by
an inline table:

```
.short min, max, default_off, case0_off, case1_off, ..., caseN_off
```

Each `*_off` is PC-relative to its own slot. There are ~307 sites
across CODE 1..23; each has a unique table. There's no shared
dispatch to lift — at every call site, read the inline table and
emit an equivalent C `switch`.

```sh
python3 tools/jt3_extract.py data/work/disasm/CODE_NN.bin --jsr-at 0xXXXX
```

prints the decoded arms and a ready-to-paste C `switch` skeleton.

### Commit cadence

One focused commit per session step — array migrations, single-
function lifts, single-tool additions. Push after each commit so
GitHub Actions catches regressions early; the test budget is small
enough that this is cheap. Avoid bundling unrelated changes even
when the diff is small.
