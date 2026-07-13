# Fan modules and "hacks" — what the port supports

Investigated 2026-07-13 against `http://frua.rosedragon.org/` (see
[[frua-fan-module-test-corpus]]; the archive is **HTTP only**). The searchable
index is `modulelist/index.php` — it has **PC and MAC** lists, each with a
"Hacked" column: **313 hacked PC modules, 148 Mac modules (27 hacked)**.

## ⚠️ There is no `.HAK` file in FRUA

Searched all 27 module letter dirs plus `hacks/`, `hacks/Archive/`, `misc/`,
`uashell/`, `modules/patches/`, `modules/unfinished/`: **zero files with `hak` in
the name.** The archive's `uashell/hackdocs.txt` states it:

> "**UA Shell is a hack manager for SSI's Unlimited Adventures.**"

`.hak` is a *Neverwinter Nights* container. The *concept* — "this module needs
extra content installed" — is real in FRUA; it just isn't a file format. A FRUA
**hack = replaced base-game files**, shipped inside the module's own `.DSN`
folder and advertised in the readme's `*HACK INFO*` block.

## ★★ CORRECTION: PC modules' custom ART DOES NOT LOAD

**An earlier version of this doc claimed "custom art works" on the strength of
THE CURSE OF THE FIRE DRAGON rendering a tree-corridor wall set. That was WRONG,
and the way it was wrong is worth remembering: the trees were the BASE GAME'S
art.** `8X8DB.CTL` holds **ten** wall sets — stone, wood, trees, … — and CURSE's
level data simply selects the tree set. It looked like custom art loading because
it looked nothing like HEIRS. Rendering "something that isn't the last design's
art" is NOT evidence that a module's art loaded.

The magic bytes are decisive:

| release | art file | magic | endian |
|---|---|---|---|
| **DOS/PC** | `8X8D1008.**TLB**` | `HLIB` | little |
| **Mac** | `8x8db3001.**CTL**` | `GLIB` | big |
| base game (Mac) | `8X8DB.CTL` / `.TLB` | `GLIB` (both) | big |

The engine reads **GLIB**, and builds design-art filenames in the **Mac**
convention (`8x8db<id>.ctl` — visible in Hatari's "have to clip 1 chars from
'8x8db3001.ctl'" warnings). A PC module's art is therefore **doubly**
incompatible:

1. **Wrong format** — `HLIB` (little-endian, VGA Mode-X 8bpp) vs `GLIB`.
2. **Wrong filename** — DOS is 8.3, so it ships `8X8D1008.TLB`; the Mac engine
   looks for `8x8db1008.ctl`. Even a format converter must also rename.

So a hacked **PC** module loads and plays, but **silently falls back to base-game
art** for every hacked asset. Nothing crashes — and nothing warns you, which is
exactly why this fooled me.

## Mac modules are the natively-correct case

`modulelist/filem.php` lists **148 Mac modules, 27 of them hacked**, mostly `.sit`
(StuffIt — same packaging as the Mac release itself, unpack per
`docs/mac-release.md`); a couple are plain `.zip`.

**THE CURSE OF YEZUKRIIS** (`mac/modules/y/yezu1mac.zip`) ships its custom art as
**`GLIB` `.CTL` with Mac-convention names** (`CPIC1001.CTL`, `BIGP0244.CTL`,
`SPRI0052.CTL`, `PICE1202.CTL` …) — precisely what the engine looks for. Verified:
it appears in SELECT A DESIGN, its areas load (CITY OF ORMAEA, THE REALMS), and
the ART GALLERY picture browser opens and renders. **0 bus errors.**

**Not yet proven:** that a *specific* YEZU custom picture displays rather than a
base-game one. The names/format match what the engine wants and nothing errored,
but I did not isolate one of its nine replaced assets and diff it against the base.
**Do that before claiming Mac-hack art support** — see the CURSE lesson above.

## Support matrix (measured)

| Payload | PC module | Mac module |
|---|---|---|
| Design data (`GAME001/GEO*/MONST*/STRG*.DAT`) | ✅ works (byte-identical DOS↔Mac) | ✅ works |
| Custom art | ❌ **ignored** — `HLIB` + 8.3 names | ⬜ format+names correct; **display of a replaced asset unproven** |
| Custom music (`.xmi` + `.PAT`/`.AD`) | ❌ ignored | — (Mac modules ship `.AD` too; unread) |
| Custom font (`.FON`), `DIFF.TBL`, CKIT edits | ❌ ignored | ❌ ignored |
| Engine hacks (UA.EXE patches) | n/a — DOS binary patches | n/a |

## What full PC-hack support would take

Not a lift — a **converter**: `HLIB → GLIB` (endian flip + Mode-X 8bpp → the Mac
piece codecs) **plus a rename** to the Mac `8x8db<id>.ctl` convention.
`tools/hlib_extract.py` already decodes both containers ([[dos-vs-mac-art-formats]]),
so the decode half exists. This is a real feature, not a decompilation task, and
overlaps the long-standing DOS-ingest idea (task #106). **Ask before starting.**
