# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

**OpenUA** — an open reimplementation of SSI's *Unlimited Adventures* engine
(FRUA, 1993) for Motorola 68k retro machines, based on a decompilation of the
**Macintosh 68k** release. It targets the Atari Falcon030 / TT030 today, with an
Amiga AGA port in progress (ADR-0012). Mac 68k → 68020/030: the CPU code carries
over; the work is retargeting the Mac Toolbox and the display/sound/input paths
behind a HAL, so one engine serves several machines. (The repo was originally
named `falcon-port`, before the second target; some tooling still uses that
name.)

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
- **Art libraries resolve design-first, root-fallback**; a module's replacement
  art lives in its `.DSN` and never overwrites the base game (ADR-0011).
- **The other Gold Box games run through a second module driver, not through
  FRUA's event table** (ADR-0023). Their MAPS are translated into the engine's
  own 6-byte cells (`tools/gbgeo.py`); their SCRIPTS (ECL bytecode) are
  interpreted natively (`src/engine/ecl.c`, host glue at the end of `boot.c`
  under "GOLD BOX SCRIPT AREAS"). See the section below before touching either.
- **The bitplane machines render natively in planes**, not through the Mac's
  chunky surface — writers stamp bitplanes at draw time and the present skips
  the conversion for the rows they own (ADR-0016). Shipping on **all three**:
  ST/STE, Amiga ECS and Amiga AGA. Falcon (VIDEL) and Amiga RTG stay chunky by
  design — they are chunky-native, so there is nothing to convert. **The TT030
  is NOT chunky** (TT-Low is 320x480 in 8 word-interleaved bitplanes). It no
  longer converts every row of every frame: #99 gave it the dirty-row present and
  found that `qd_set_palette`'s unconditional `qd_touch_all()` was forcing a full
  frame ~1x per present, which also blocked the #152 clean-present skip. A new
  `dsp_backend_t.hw_palette` flag (set ONLY by the TT, whose planes hold the index
  and whose palette is hardware) took the same drive from 95,092 converted rows to
  5,792 — 619 of 651 presents now skipped entirely — with six frames
  pixel-identical. **The draw-time WRITER half is done on the TT too**
  (`t_dt` in `platform/display_tt.c`): every build defines `FRUA_PLANAR`
  unless `PLANAR=0` is passed, the 020 one included. An earlier revision of
  this file said the opposite; it was stale. The 2026-11-16 TT walk profile
  (`tools/profile/walk_profile.sh`) and the fixes that came out of it are in
  the "Profiling" section below.
  Note that "native planar" removes the conversion only for rows a writer
  actually stamped. An earlier measurement (2026-07-26) put that at "~47% of
  presented ST rows still convert", attributed to reband epoch-resets — **read
  that as a BOOT figure, not a steady-state one.** Re-measured the same day
  with the phases separated: the boot converts ~186 rows per present and
  re-bands 11 times, while the post-menu screens converted **zero** rows and
  re-banded **zero** times. The profiler's window is 16 presents and the boot
  is 16 presents, which is how the boot came to stand in for the whole run. The
  play loop itself is still unmeasured — see the #90 section of
  `docs/planar-plan.md`.

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

## Reference tooling (MCP)

Two local MCP servers back this work — check whether they're connected (their
`mcp__…` tools appear in the session); if a needed one is missing, register it
and reconnect the session (MCP servers load at session start).

- **`frua-reference`** — the FRUA/Mac-Toolbox corpus: `jt_lookup`, `fc_groups`,
  `toolbox_search`/`toolbox_get`/`toolbox_list`, plus disasm/symbol/trace tools.
  Use before lifting Resource-Manager / purgeable-handle / FC-cache code.
- **`atari-compendium`** — Scott Sanders' *Atari Compendium* (842 pages: XBIOS,
  GEMDOS, BIOS, VDI, AES, Line-A, hardware registers) as a searchable index.
  Tools: `search`, `get_page`, `table_of_contents`. Reach for it on ANY
  Atari-hardware/OS detail — `Blitmode`/BLiTTER, `Setscreen`/`Vsync`, VDI
  blits, colour-register/copper timing behind the ST/StE/Falcon display work.
  Server: `tools/atari-compendium-mcp/` (see its README). Register with:
  `claude mcp add atari-compendium -s user -- \
   tools/atari-compendium-mcp/.venv/bin/python \
   tools/atari-compendium-mcp/server.py` (use absolute paths). The PDF + its
  text cache are NOT committed (copyright, like `data/`); only the server code.

## Vendored subtree: `third_party/c2p-68k`

The chunky→planar converter (`c2p32.h`, `c2p4st.h`, `c2p_amiga.*`) lives in a
**git subtree**, not in `platform/`. It is shared with another project, so this
repo is a CONSUMER — upstream is the source of truth.

```sh
# pull upstream changes in
git subtree pull --prefix=third_party/c2p-68k c2p-68k main --squash
# push local edits back out (do this, or the copies diverge silently)
git subtree push --prefix=third_party/c2p-68k c2p-68k main
```

Upstream is <https://github.com/slaapliedje/c2p-68k> (MIT). A fresh clone of
OpenUA has no `c2p-68k` remote — the subtree's code is committed in-tree so the
build works regardless, but before the first pull/push you need:

```sh
git remote add c2p-68k git@github.com:slaapliedje/c2p-68k.git
```

- **Do not re-add local copies** of those files under `platform/`. Edit them in
  `third_party/c2p-68k/` and push upstream.
- The subtree carries its OWN tests; `make test` runs them via an explicit
  `third_party/c2p-68k/tests` path. Adding tests there is free, but they only
  run because that path is named.
- `third_party` is in the BUILDSTAMP purge and in `clean`. It has to be: the
  subtree's `c2p_amiga.o` would otherwise survive a `CPU68K` switch and link
  68020 code into a 68000 binary — exactly the staleness the stamp exists to
  prevent.
- Licensing differs deliberately: OpenUA is GPL-2.0, the subtree is MIT (see
  `third_party/c2p-68k/PROVENANCE.md`).

## Layer rule

`src/engine/` (engine) → `compat/` (Mac Toolbox shim) → `platform/` (HAL) →
TOS. Only `platform/` may know which machine it runs on. Engine code must not
call XBIOS/TOS directly — go through the shim or the HAL.

## Build / test

```sh
make              # build frua.prg (soft-float — runs on Falcon030 and TT030)
make FPU=1        # FPU-required TT030 variant
make CPU68K=68000 # bare-68000 ST/STE build — NATIVE PLANAR (see below)
make run          # boot in Hatari (Falcon mode)
make test         # host-side pytest suite over tools/ (tests/, synthetic data)
```

`CPU68K=68000` implies **`-DFRUA_PLANAR`** (ADR-0016): the low-end bitplane
build stamps planes at draw time and page-flips instead of running a
chunky→planar conversion each present. This is what the ST/STE and Amiga ECS
zips ship, so the build you boot in an emulator is the build that ships —
verified byte-identical to the `release-ste` / `release-amiga-ecs` output.
`make CPU68K=68000 PLANAR=0` restores the chunky path for A/B comparison.

**`-DFRUA_PLANAR` is on in EVERY build unless `PLANAR=0`** (Makefile, `ifneq
($(PLANAR),0)`), the 020 one included — the TT's draw-time writers need it.
What stays chunky is the Falcon's VIDEL backend and Amiga RTG, by design.

## Branches

Work lands on **`main`**. `unix-port` was the working branch until
2026-11-16, when it was fast-forwarded into `main`; it has not moved since.

## Profiling and the row police

Measure before optimising, and prove a display change exact before measuring
it. The instruments, newest first:

- **`tools/profile/walk_profile.sh tt|falcon|ste <outdir>`** — a Hatari cycle
  profile of a scripted HEIRS walk, cut on engine ticks. Build with
  `EXTRA_CFLAGS="-DFRUA_AUTOPLAY -DFRUA_AUTOWALK -DFRUA_AUTOWALK_TREASURE
  -DFRUA_WALKPROF"`; aggregate with `tools/profile/walk_aggregate.py`. Three
  runs agree to about 1%. Prefer it to `tools/profile/st_profile.sh`, whose
  xdotool drive is paced by host time (about 15% run-to-run) and whose
  aggregator assumes the STE's load address.
- **The police** — each proves a dirty-row or partial present left nothing
  stale. Run the one for the backend you touched, AND the others if the change
  is in shared code (`compat/`, `src/engine/`):
  - `-DFRUA_TTCHK` (TT): reports any row nobody announced that no longer
    matches what is on screen, with the rows that were announced.
  - `-DFRUA_VIDELVERIFY` (Falcon): compares every written page with a
    from-scratch blit.
  - `-DFRUA_DIRTYCHECK -DFRUA_STPROF` (ST/STE; ECS has its own): the count is
    only PRINTED under `FRUA_STPROF`.
- **A time-boxed window is partly idle.** Freed cycles show up as more idle
  polling, not a shorter total; ask what fraction of the window was doing the
  thing you care about.
- **Never `pkill hatari` / `pkill amiberry`.** Other sessions on the machine
  run their own emulators; kill only PIDs you started. The older scripts and
  the skill drivers kill by name — do not run them unmodified. The native
  amiberry also ignores `DISPLAY` unless `SDL_VIDEODRIVER=x11` is set and
  `WAYLAND_DISPLAY` is unset; without both it opens on the real desktop.
- **A helper that greps build output for errors must also check make's exit
  status.** One that grepped for `" error "` (GCC prints `error:`) let two
  "verification" runs go by on a stale binary.

What the 2026-11-16 pass found, so it is not rediscovered:

- **Colour cycling drives most presents in the dungeon** (a few CLUT entries
  every 11 ticks). On the Falcon each step re-blitted the whole truecolour
  frame; `platform/display_videl.c` now keeps per-page debt and blits only
  the 80-pixel segments that use a changed colour (`video.cfg` `partial=off`
  restores the full blit). On the TT a palette write is hardware and free.
  On AGA the write was free but a full 200-row present still followed it;
  `aga_present` now keeps per-page row debt and copies only what a page
  owes (`-DFRUA_AGAVERIFY` is its police; `video.cfg` `agarowdebt=off`
  restores the old present). ECS ignores small writes but still presents;
  that present now costs about 21 ms instead of 200, because the ink-hold
  and ink-adoption scans walk only announced rows that CHANGED (the pointer
  announces 16 rows every present).
- **The 88-pixel walk viewport is two 32-pixel transposes and a 24-pixel
  tail on every row**, and the tail was a per-pixel, per-plane bit loop. It
  is a table lookup now (`third_party/c2p-68k`): viewport present 24 -> 18
  ticks on an A500 build, 5 -> 2 on an A1200 build. The table is slower than
  the transpose for whole frames, even on a 68000 — do not swap them.
- **The 3D view was the walk on every machine, and three per-pixel loops
  were most of it** (2026-11-17, `render_3d_faithful` and the tile blit):
  the static backdrop was rescaled and stored through a bounds-checked
  `map_px` every step (now scaled once and laid down by rows;
  `-DFRUA_NOBDCACHE` opts out); a coverage bitmap only `FRUA_VPPLANAR`
  reads was filled per pixel in shipping ST builds; and `planar_tile_blit`
  folded its clip into the mask one pixel at a time. Scripted walk: 68000
  ST 452 -> 255 s, Falcon 50.8 -> 39.5 s, TT 31.0 -> 25.1 s; step render
  on the A500 build 24-29 -> 13 ticks, on the A1200 build 6 -> 3. To find
  the next one, list the hot instructions inside a function from the Hatari
  per-address dump rather than trusting the per-function table — inlined
  loops hide in the caller's exclusive time.
- **`FRUA_DIRTYCHECK` on ECS reports 63 unannounced rows (24..95) once in
  the HEIRS walk, right after a reband cache hit.** It predates the
  2026-11-17 changes (the unmodified build reports the same 63) and is not
  yet explained.
- **`l2d4e` announces the piece's rows, not the clip.** The compass's
  full-screen clip had every walk key marking all 200 rows.
- **The software cursor's 16 rows stay announced across a present** — the
  screen shows the pointer, the surface does not.
- **`port_draw_compass` redraws only when the facing or the pixels under it
  changed** (it keeps a copy of what it drew; `-DFRUA_NOCOMPASSCACHE` opts out).

## Gold Box script areas (ADR-0023)

FRUA never played the Krynn games or any other Gold Box title. The port is
growing that: Dark Queen of Krynn's opening plays from the game's own files.

- **Converter:** `tools/gbgeo.py GEO.GLB --design OUT --ecl ECL.GLB --start N`
  writes an ordinary design folder plus `GBDnnn.DAT` (per area slot: the map's
  cell-descriptor plane), `ECLnnn.DAT` (per SCRIPT, numbered as the game
  numbers it) and `GBMAP.DAT` (map numbers to area slots). Scripts and maps
  are numbered separately in these games — Dark Queen has 47 scripts over 20
  maps.
- **`tools/ecl.py`** disassembles the scripts (Champions, Death Knights, Dark
  Queen each have their own operand table; a wrong one shows as undecodable
  or doubly-claimed bytes). The opcode tables and semantics are the Greybox
  project's (`kblood/Greybox_CombinedGoldbox`, 0BSD) — go there before
  guessing at an opcode.
- **`src/engine/ecl.c` is plain C99 with no engine, shim or platform headers**,
  so it builds on the host for `tests/test_ecl_c.py`. Keep it that way: what a
  script does TO THE GAME goes through the one host callback.
- **The engine's map holds 784 cells** (`GEO_MAX_CELLS`), not the Mac's 576.
  A FRUA area is still held to the Mac's file sizes; a larger map arrives as a
  "wide area" (`docs/geo-format.md`). The area EDITOR stays a 576-cell tool.
- **A cell's four edge bytes are N, E, S, W** (the engine reads byte
  `facing >> 1`), and `tools/geo.py`'s `row` is the east-west axis. A square
  test room cannot tell the orders apart — use an asymmetric map.
- **Wired so far:** text, menus (on the engine's button bar, JT[182]), yes/no,
  number input (JT[891]), script chains, map loads, party placement (Dark
  Queen's position variables only), pictures, fights, treasure, temples,
  shops, inns, training halls. **Not wired:** string input, surprise and
  approach distance, random items, PROGRAM 1 and 3; script memory is not
  saved. A script that needs one of those takes a wrong turn.
- **Shops are opcode 0x3c** (PROTECTION in Greybox's table) with a shop type
  1..9; the stock lists live in DQK.EXE and `gbgeo.py --exe` copies them out
  as GBSHOP.DAT (found by shape, not address). They are sold through `jt183`,
  FRUA's shop. **Inn = PROGRAM 9** (`l473e`), **training = PROGRAM 0**
  (`jt918`). Each keeps the script's own picture and greeting. The price
  level (area byte 0x28) is read as sixteenths of list price and the
  training class mask (0x30) is passed to FRUA's hall as-is — both are
  inferences from the scripts, not read out of the game's code.
- **A converted game uses its OWN item table.** FRUA loads item.dat once and
  a design cannot replace it, so `gbs_items` reads the design's GBITEM.DAT
  over it when a script area comes up and restores FRUA's when an ordinary
  one does. Monster items, treasure and shop stock are all numbers into it.
  The item TYPE table (ITEMS.DAT, 64 bytes differ) is still FRUA's.
- **Fights run through `l159a`, FRUA's combat event handler,** on a 20-byte
  record built from the groups LOAD_MONSTER queued (`gbs_combat`). The
  monsters are the game's OWN: `tools/gbmon.py` rewrites each Dark Queen
  record (MONCHA.GLB) in FRUA's 450-byte layout as the design's
  `MONSTnnn.DAT`, which the engine reads before the stock library. The field
  map is in that file's docstring and was derived from the 29 monsters both
  games share, not guessed — re-run `gbmon.py --compare MONST.GLB` after
  touching it.
- **TREASURE stages, COMBAT hands out.** The original adds to a pending pile
  and shows nothing until the next COMBAT; FRUA has the same pile (-25314
  money, -25310 gems, -25306 jewelry, items at -25302), so the script stages
  into it with `jt187` and `jt930` shows it — alone when COMBAT has no
  monsters, with the monsters' own loot when it has. With no monsters and
  area byte 0x14 set, COMBAT is a temple instead (`l216a`; the level and
  price band are this port's choice, the game has neither). Area byte 0x31
  selects a third screen that is not identified yet.
- **To test a script opcode in the engine, write a script.** `tests/test_ecl.py`
  has the assembler (`block`, `imm`, `text`); "GBS2", the dialect byte, three
  zeros and the block is an `ECLnnn.DAT`. Build with `-DFRUA_CBTPLAY` to have
  the fight play itself — it also answers every text prompt, so make the
  script end somewhere visible (a NEWECL into a menu).
- **Pictures go through `l442e`, FRUA's own event-picture painter.** Dark
  Queen's PICA/PICB/PICC/BIGPIC/SPRIT are FRUA's container and names, so
  `gbgeo.py --pictures DISK2` only converts them into the design folder. The
  one difference is which numbers live in which file (Dark Queen: PICA below
  128, PICB to 150, PICC from 151), bridged by `g_gbs_pic_letter`. Do NOT
  copy the game's FRAME/wall/combat libraries into the design: FRUA's screen
  is built from its own pieces of those and the button bar loses its plates.
- **Three opcode NAMES in Greybox's table are Champions' and wrong for Death
  Knights and Dark Queen:** 0x1d is "press Return", 0x22 a yes/no that sets
  the == / != flags, 0x23 "clear the text box and EXIT" (read out of
  DQK.EXE; about 2,900 uses in Dark Queen). Skipping 0x23 runs a script on
  into the next cell's code. Read the handler before trusting a mnemonic.
- Game data is the user's own (the Steam DOS releases work); never commit it.

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
  **On the AMIGA side you must pass `-m m68k:68020`**: an Amiga hunk binary
  carries no CPU tag, so objdump falls back to 68000 and silently MISDECODES
  020 instructions. The AGA build shows 433 `mulsl` / 233 `bfextu` with the
  flag and exactly zero without it — i.e. the bare recipe reports a perfectly
  good 020 build as broken.
  **Run it on the OBJECT FILES, not on `frua.prg`, when asking the INVERSE
  question** ("is this 68000 build really 68000?"). A GEMDOS PRG's TEXT segment
  also holds the read-only data GCC parks in `.text`, and objdump disassembles
  that too: a clean `CPU68K=68000` build reports ~6 phantom `bfextu`/`bfins`
  hits, all of them a monotonic lookup table (`e9ba,61c6 / e9c2,61d4 / …`)
  being read as opcodes. The honest check is
  `objdump -d --section=.text <each .o> | grep -cE 'bfextu|bfins|muls\.l|divs\.l'`,
  which sums to exactly 0 on a real 68000 build — verified 2026-08-07 before
  putting a binary on real Mega STe hardware.

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
