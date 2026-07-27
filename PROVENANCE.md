# Provenance

These files were extracted on 2026-07-26 from **OpenUA**, an open
reimplementation of a 1993 Macintosh game engine for 68k retro machines
(Atari ST/STE/TT/Falcon, Amiga OCS/ECS/AGA/RTG).

OpenUA is licensed **GPL-2.0**. This extraction is **MIT**.

## Why that is legitimate

Copyright in every extracted line is held by a single author, who made the
relicensing decision. Verified before the first commit — per-file, across the
full history including renames:

    for f in platform/include/c2p32.h platform/include/c2p4st.h \
             platform/amiga/c2p_amiga.c tests/test_c2p4st.py \
             tests/test_c2p_amiga.py; do
        git log --follow --format='%an' -- "$f" | sort -u
    done

(`--follow` accepts exactly one pathspec, hence the loop.) The only other
attribution in those commits is `Co-Authored-By` trailers naming an AI
assistant, which do not create a third-party copyright interest.

None of the extracted code derives from any third party's work, and none of it
contains or embeds game data of any kind.

## Relationship to the upstream copy

OpenUA still carries its own copies. **This repository is intended to be the
source of truth**; the aim is for OpenUA to consume it (git subtree) rather
than maintain a fork. Until that happens the two will drift, so if you change
something here, port it back — and check `git log` on both sides before
assuming they match.

Differences at extraction time (everything else is byte-identical):

- `src/c2p_amiga.c` drops `#include "display.h"` — it was spurious, nothing in
  the file used it.
- `src/c2p_amiga.c` gains `#include "c2p_amiga.h"`, so the new public header
  and the in-file prototypes must agree or compilation fails.
- `include/c2p_amiga.h` is new: upstream the `.c` self-declared its prototypes
  and there was no public header.
- The tests' include paths point at `include/` and `src/` instead of
  `platform/include/` and `platform/amiga/`.
