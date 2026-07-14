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

### ⚠️ Wall libraries do NOT convert yet (found 2026-07-13 with Pool of Radiance)

`8x8db`/`8x8dc` are a **container-of-containers** (10 wall SETS x 48 pieces).
`convert()` now recurses into them — but installing a converted wall library over
the base game's still renders a **BLACK 3D view**. 423 of Game39's 432 wall pieces
parse as plain tiles; **9 fall into `_convert_entry`'s "opaque" fallback**, which
copies a payload it does not understand straight through. That fallback is proven
only for the *picture* palettes of the Yezukriis pair, and this is what it looks
like when the assumption is wrong. The black view says something further is also
wrong — the per-set colour-range/palette is the prime suspect
(`docs/glib-palette-subsystem.md`). **Do not ship converted wall art.**

### ★ How a "hack" is actually installed

A whole-library hack is **NOT a design-dir override**. Dropping `8x8db.ctl` into
the `.DSN` folder changes nothing — the engine builds per-id names
(`8x8db<id>.ctl`) and otherwise reads the base library from the **install root**.
A hacked module's replacement libraries must overwrite the ROOT files
(`data/work/gamedata/8X8DB.CTL`). That is what "replaced base-game files" means,
and it is why art-hacked modules need an install step, not just a convert step.

**Refused (by design, loudly — never silently mangled):**
- **piece type 2 (RLE)** — the two releases use *different* codecs. The Mac side
  is PackBits (`jt1171` = `_UnpackBits`); the DOS side is not decoded yet. In
  practice this is exactly the **BIGP** (big event picture) payloads — 4 of
  GIANTS' 79, and the reason its merchant portrait still falls back to base art.
  **This is the remaining work.**
- **wall sets** — DOS 8.3 truncates *both* `8x8db<id>` and `8x8dc<id>` to
  `8X8D<id>`, so the Mac target cannot be inferred. Needs a Mac module that ships
  wall art to disambiguate. `mac_name()` raises rather than guess.

## ⛔ CLOSED — the "big-picture colour cast" NEVER EXISTED (2026-07-13)

**Not a bug. Not a conversion bug, not a palette bug, not a bug at all.** The
port's big-picture render is **bit-exact** — 0 of 145,920 pixels wrong against an
offline decode of the same asset (`tools/screen_diff.py`; full write-up in
`docs/glib-palette-subsystem.md`).

Two things fooled me, and both are worth internalising:

1. **The picture really is magenta and cyan.** Base big-picture set 6 is a
   blighted enchanted forest the gallery names **`WALKING FOREST`**. The other
   seven base pictures render perfectly naturally. I flagged a bug off a *glance*
   at stylised 1993 fantasy art.

2. **★ The sighting that started it was a SILENT FALLBACK.** "GIANTS' intro
   picture has a cast" — except GIANTS' `BIGP0245.TLB` is **HLIB**, which the
   engine cannot read, so it fell back to **base id 245 = WALKING FOREST**.
   I was looking at base-game art the whole time. GIANTS' own big pictures decode
   to three natural overland maps and a brown title card.

   **This is the CURSE tree-walls trap AGAIN** (see the correction above), in a
   different subsystem. When a PC module's art looks wrong, **suspect the silent
   HLIB fallback before the renderer.** Nothing warns you — that is the real
   defect here, and it is a *missing warning*, not a broken pipeline.

### ✅ `-DFRUA_ARTTRACE` — the fallback is no longer silent

The fallback itself is **faithful** (`l33ac`: a missing per-id override just means
"use the base library"; the Mac does exactly this) and must NOT be "fixed". What
was missing is *visibility*. Build with the trace and it names itself:

```sh
make EXTRA_CFLAGS=-DFRUA_ARTTRACE     # NB: EXTRA_CFLAGS, not CFLAGS_EXTRA
```

With **GIANTS** selected, opening BIG PICTURES in the ART GALLERY now logs:

    ART: no override, using base library: GIANTS.DSN:bigpic0245.ctl

and the screen draws base id 245 — **WALKING FOREST** — which `screen_diff.py`
confirms is bit-exact base art (0/145920 px) and 100% *unlike* GIANTS' own
`BIGP0245` title card (145920/145920 px). Both halves proven, on screen.

**Run this before concluding anything about a module's custom art.** It covers
both resolvers: `l33ac` (per-id FC art — pictures, wall sets) and `ua_open_art`
(whole-file art — FRAME/MENU/GEN/TITLE/BACK/ALWAYS).

### ~~The colour-table "magic" byte~~ — EXPLAINED, and NOT the cause (retracted; the cast itself is now closed too)

I flagged the magic byte (200 in the base BIGPIC sets vs 8 in design pictures) as
a lead. **It is a dead end, and the engine says why.** `jt993` ("TNPalette"):

```c
if ((hdr[7] & 15) != 8) { l036a("Invalid TNPalette call ..."); return; }
```

**The low nibble is the BLOCK TYPE; 8 = "palette".** That is what the hackdocs'
unexplained "value should be either 8 or 24" actually means — both have low nibble
8 (24 = 0x18). And it is why the Mac's **200 (0xC8) is equally valid**:
`0xC8 & 15 == 8`. The high nibble is a format/flags field. `art_convert` already
preserves the low nibble and remaps the high nibble, so converted tables pass the
check. All three values are accepted. **Not the cast.**

*(The online docs never explain this. All 57 hackdocs were swept: they assert the
value and stop, and they are explicitly DOS-only, so the Mac's 0xC8 is outside
their scope entirely. The lifted engine is the better reference — use it first.)*

### The rest of the header, decoded from jt993/jt1069 — ✅ CONFIRMED CORRECT

    hdr[0..1]  flags   (TLBFORM calls it the "cycling value")
                       bit0 -> the CLUT window is EXPLICIT (else 0..256)
    hdr[2..3]  start   first CLUT index      } only when bit0 is set
    hdr[4..5]  count   number of colours     }
    hdr[6]     ncopy   count of 4-byte cycle/remap records after the RGB block
    hdr[7]     low nibble = block type (8 = palette)

Then `jt1069(start, count, palbuf, ncopy, rembuf)` allocates the ranges.

**This decode is now proven**, not inferred: `tools/screen_diff.py` uses exactly
these fields to render the base big pictures offline, and the result is
**bit-identical to what the engine puts on screen**. Base and fan pictures request
the same window (bit0 set, start=32, count=224 — matching UAPALETT.TXT). `ncopy`
differs (7 cycle records in base set 6, 0 in POR's) and that is **fine** — cycle
records animate a range, they don't change base colours.

### The master libraries carry an ID TABLE — entry 0, when byte[11] == 1

    base BIGPIC.CTL: id 240->set 1, 241->2, 242->3, 243->4,
                     244->5, 245->6, 246->7, 247->8

`u16 count`, then `count` × `(u16 id, u16 entry)`. This is how `BIGP0245.TLB`
resolves, and it is what makes the silent fan-art fallback land on a *specific*
base picture (id 245 → set 6 → WALKING FOREST). **Do not confuse byte[10] and
byte[11]** — they are two independent bytes, not a u16; byte-swapping them is what
caused the black-walls bug.
