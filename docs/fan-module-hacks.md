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

## ✅ The converter — `tools/art_convert.py` (2026-07-13)

**Derived from ground truth, not guessed.** "The Curse of Yezukriis" ships as
**both** a PC module (`pc/modules/y/yezu.zip`, HLIB) and a Mac module
(`mac/modules/y/yezu1mac.zip`, GLIB) — the *same nine assets, authored once*.
Diffing the pair gave the exact transform, and
`tests/test_art_convert.py::test_converts_real_dos_art_to_byte_identical_real_mac_art`
converts the real DOS art and asserts it equals the real Mac art **byte for
byte** (and round-trips back). That test runs whenever the pair is staged.

**The transform**

| | HLIB (DOS) | GLIB (Mac) |
|---|---|---|
| container | little-endian | big-endian |
| offset table | *identical values* — file size unchanged | |
| entry `u16 rows, i16 xhot, i16 yhot` | byte-swapped | |
| entry `byte[6]` | `W/4` (Mode-X per-plane stride) | `W/8` |
| entry `byte[7]` | low nibble = piece type; high nibble **`0x1`** | high nibble **`0xc`** |
| **pixels** | **VGA Mode-X, 4 unchained planes, PLANE-MAJOR** (plane `p` = columns `x%4==p`) | **linear rows of `W` bytes** |

`W` = row width in bytes, padded to a multiple of 4. The pixel shuffle is the
part a byte-swap alone never fixes.

```sh
python3 tools/art_convert.py data/work/gamedata/SOME.DSN/*.TLB   # -> *.ctl
```

Run over AGAINST THE GIANTS: **75 of 79 art files converted**, 0 bus errors.

**Refused (by design, loudly — never silently mangled):**
- **piece type 2 (RLE)** — the two releases use *different* codecs. The Mac side
  is PackBits (`jt1171` = `_UnpackBits`); the DOS side is not decoded yet. In
  practice this is exactly the **BIGP** (big event picture) payloads — 4 of
  GIANTS' 79, and the reason its merchant portrait still falls back to base art.
  **This is the remaining work.**
- **wall sets** — DOS 8.3 truncates *both* `8x8db<id>` and `8x8dc<id>` to
  `8X8D<id>`, so the Mac target cannot be inferred. Needs a Mac module that ships
  wall art to disambiguate. `mac_name()` raises rather than guess.
