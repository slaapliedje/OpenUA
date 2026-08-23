---
name: port-ssi-mac-68k
description: The OpenUA porting kit — reuse this repo's toolchain, Mac Toolbox shim, decompilation workflow, and HAL to port another SSI / Mac 68k game to Atari ST/Falcon or Amiga. Use when starting or planning a port of another Gold Box / SSI title (Pool of Radiance, Curse of the Azure Bonds, Champions of Krynn...) or any THINK C Mac 68k program.
---

OpenUA is not just a FRUA port — it is a **kit for putting a Mac 68k game on
the Atari and Amiga**, built and verified the hard way. The Amiga got the whole
Gold Box line; the ST only ever got Curse of the Azure Bonds. This skill maps
what carries over to the next title and where the real work starts.

## What is reusable AS-IS (the platform layers)

The layer rule (`CLAUDE.md`): engine → `compat/` (Mac Toolbox shim) →
`platform/` (HAL) → TOS/AmigaOS. Everything below the engine is game-agnostic:

- **Toolchain**: m68k-atari-mint GCC with the soft-float `-m68020-60` multilib
  (`docs/toolchain-softfloat-020.md`), Bebbo m68k-amigaos
  (`docs/toolchain-amiga.md`, `-fbbb` workaround). Flag discipline + the
  objdump 020-opcode verification recipe live in `CLAUDE.md` — including the
  two false-positive traps (data-in-.text phantoms; Amiga hunk needs
  `-m m68k:68020` or objdump silently misdecodes).
- **compat/**: FSOpen/NewHandle/QuickDraw/Dialog/List/Sound Manager shims over
  GEMDOS + AmigaOS. Engine code keeps Mac spellings; GEMDOS errors translate at
  the boundary (`gemdos_to_oserr`). A new game reuses this wholesale and only
  extends coverage (update `docs/toolbox-mapping.md` per manager).
- **platform/**: display HAL (VIDEL / TT / ST planar / Nova / ECS / AGA / RTG
  backends), sound (Falcon DMA, Paula with the rewritten integer mixer), input,
  the c2p subtree (`third_party/c2p-68k` — a SHARED subtree, edit upstream).
  See the `planar-display-kit` skill for the 16/32-colour machinery.
- **Harness**: `run-falcon-port` / `run-amiga-port` skills (headless Hatari /
  amiberry driving), `-DFRUA_RNGSEED` deterministic A/B, `FRUA_AUTOPLAY`
  scripted playthroughs, the profiling rigs (`FRUA_STPROF`, `FRUA_AMIGAPROF`,
  `QUANT_PROF`).
- **Media + installers**: see the `hardware-media-kit` skill.

## The decompilation workflow (per game)

Works for any THINK C Mac 68k binary (A5-relative globals, CODE segments,
`jsr JT[n]` jump table, JT[3] inline switch tables):

1. `tools/dis68k.py <resource-fork>` — objdump-driven annotated disassembly of
   every CODE segment + the jump table. Listings are git-ignored; the lifted C
   is the committed work (ADR-0009).
2. `tools/jt3_extract.py CODE_NN.bin --jsr-at 0xXXXX` — decode a THINK C
   inline switch and emit the C skeleton.
3. Lift levels 1/2/3 (PROBE stub / structural skeleton / full lift) —
   `CLAUDE.md` "Decompilation workflow". Match depth to the session, not the
   function's importance.
4. The traps that cost weeks here (all in `CLAUDE.md` + memory): the
   `lXXXX`=`JT[n]` alias map (same function two names — regenerate with
   `tools/gen_jt_aliases.sh`), the same hex offset recurring across CODE
   segments (different functions — key on `(CODE, offset)`), JT[452] varargs
   pushing right-to-left, and the A5-world replay (`data_pool_replay`:
   zero-fill + DATA blit + DREL relocs, non-zero scalars re-seeded at boot).
5. An **oracle build** is worth setting up early: FRUA had the Mac releases
   under Mini vMac / Basilisk II and the DOS release under DOSBox
   (`docs/dos-reference-recording.md`) — every hard disagreement was settled
   by running the original, never by reasoning about it.

## The Gold Box angle (Pool, Curse, Krynn...)

- **Art formats are already cracked here**: DOS `.DAX` (`docs/dax-format.md`,
  decoder `tools/dax_decode.py`, verified against Champions of Krynn and Death
  Knights of Krynn) and Amiga `.DAA` (`docs/daa-format.md` — big-endian planar
  cousin). FRUA's own `.TLB`/HLIB↔GLIB converter core is
  `src/convert/artconv.c` (bit-exact both directions, used by uainst/uaconv).
- **The fastest "Pool of Radiance on an ST" is not a new port**: the classic
  campaigns exist as FRUA fan-module recreations, and OpenUA plays DOS fan
  modules on the ST today (`uainst` installs from the ZIP, converting art
  in-place — ADR-0013/0014). Check the fan corpus before committing to a
  decompilation.
- **A true Gold Box engine port** would decompile a Mac Gold Box release with
  the workflow above (they are THINK C like FRUA), reusing compat/ + platform/
  unchanged. The engine lift is the whole cost; budget it like OpenUA's
  (hundreds of sessions), not like a build fix.

## Ratified decisions worth inheriting

Port from the Mac release, not DOS (ADR-0001 — 68k asm carries over); hybrid
lift (ADR-0002); shim-first (ADR-0003); runtime before editors (ADR-0008);
resource fork as a flat `(type,id)` archive via `tools/rsrcpack` (ADR-0007).
Read `docs/decisions.md` before diverging — each ADR records why the
alternative lost.
