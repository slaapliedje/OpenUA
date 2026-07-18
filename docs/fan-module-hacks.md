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
| Custom art — **as shipped** | ❌ **silently ignored** — `HLIB` + 8.3 names; falls back to base art with NO warning (use `-DFRUA_ARTTRACE`) | ⬜ format+names correct; display of a replaced asset unproven |
| Custom art — **after `tools/art_convert.py`** | ✅ **LOADS AND RENDERS** — proved end-to-end with Pool of Radiance (see the play-test below) | n/a (already GLIB) |
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

### ✅ Wall libraries DO convert (the "black 3D view" was the byte[10]/[11] swap)

*(This section used to say "wall libraries do NOT convert — do not ship converted
wall art." **That is stale.** Both the black-walls bug and the design-dir install
problem were fixed the same day; POR now walks Phlan on its own converted walls.
Left here because the stale version cost real time and the correction is the
useful part.)*

`8x8db`/`8x8dc` are a **container-of-containers** (10 wall SETS × 48 pieces), and
`convert()` recurses into them. The **BLACK 3D view** was not a palette problem at
all: bytes **[10] and [11] are TWO INDEPENDENT BYTES** (unused; ID-table magic) —
**not a u16** — and I was byte-swapping them, which moved the ID-table magic into
the unused slot. Pictures have magic = 0, so the swap was a no-op there and the
byte-exact ground-truth test never caught it; master libraries always have
magic = 1. *A ground-truth test only covers what the ground truth contains.*

### ★ How a "hack" is actually installed — design-dir override, since ADR-0011

*(Also formerly stale: "a whole-library hack is NOT a design-dir override —
replacement libraries must overwrite the ROOT files." **ADR-0011 changed this.**)*

`ua_open_art()` and the `l33ac`/`jt987` path now resolve **design-first, then the
install root**, so a module's `8x8db.ctl` in its own `.DSN` is picked up *without*
overwriting the base game. Confirmed live with `-DFRUA_ARTTRACE` (POR's per-id
probes fall back, then `8x8db.ctl` resolves to POR's own converted library).
Convert in place; no install step, and the base game is never touched.

### ✅ METHOD 23 SOLVED — the last art codec (2026-07-18)

The "DOS sweep layout is NOT SOLVED" refusal is retired. Solved against
the Mac POR edition after every parametric guess plateaued at ~50%:
**sweep p starts at x = p+1; literals advance 4; a skip byte v advances
4·(256−v)** — DRAW23.TXT carries TWO off-by-ones (257→256, and the
"+1 starting position" applies from the first sweep). Proof: the only
law with ZERO double-written pixels across POR's 62 method-23 items,
and decode→re-encode reproduces **SSI's own DOS bytes exactly for
57/62** (the rest differ only by 35 clipped x==W encoder edge pixels).
The ~50% pixel residual vs the Mac render is the Mac edition's own
re-dithered art. Method 25 (animation frame tables) fell with it —
byte-identical payloads, header-only re-encode. POR now converts
**189/191 colour + 187 mono**; its whole-library FRAME UI hack loads.
Remaining: ~~only the CBODY/COMSPR unknown-0x00 entry variant~~ —
**CLOSED same day (`a2055dd`)**: two formats behind one error. Item 0 is
a TYPE-128 composite-body table (bytes [6:8] are ONE u16, payload a u16
array — swap everything); the double-payload image entries are AND+OR
mask pairs, each plane Mode-X-shuffled independently. Proof:
`convert(CBODY.TLB) == the real Mac CBODY.CTL` byte-for-byte, both
directions, both files, in the suite. **POR: 191/191 colour. NOTHING in
the fan-art corpus is refused any more.**

**~~Refused~~ — both former refusals are now CLOSED (2026-07-17):**
- ~~**piece type 2 (RLE)**~~ — **in** since `7a0a538`: drawing method 18
  (DRAW18.TXT run codec, shared by both releases) and method 23 (the
  skip/literal transparent codec, DRAW23.TXT — a *different* codec, and
  lumping it in with 18 is what broke POR's animated pictures) are both
  decoded and re-encoded.
- ~~**wall sets**~~ — the DOS 8.3 collapse (`8x8db<id>`/`8x8dc<id>` →
  `8X8D<id>`) is **reversible**: the engine's own wall loader (CODE 7
  `L6eea`) derives the letter from the SET-ID BAND — `(id < 10) ? 'b' :
  'c'` — before probing the per-id override. `mac_name()` now applies the
  same rule (`cf87c49`). BEOWOLF converts **125/125** art files, walls
  included; no Mac wall-art module was needed after all.

### ✅ Second end-to-end proof — BEOWOLF walks its own graveyard (2026-07-17)

The wall-name rule's first live outing. `make gamedata DSN=BEOWOLF.DSN`,
convert the STAGED copy in place (125/125), build with
`-DFRUA_AREATEST -DFRUA_ARTTRACE -DFRUA_SKIP_ENTRY_EVENTS`, boot Falcon:

    ART: DESIGN OVERRIDE loaded: BEOWOLF.DSN:8x8db1008.ctl
    ART: DESIGN OVERRIDE loaded: BEOWOLF.DSN:8x8db3003.ctl
    ART: DESIGN OVERRIDE loaded: BEOWOLF.DSN:8x8dc2016.ctl

— all three wall groups from the design's own converted art, **one from
each letter band** (the L6eea rule live), with FRAME/MENU/GEN/TITLE/BACK
correctly falling back to root. The 3D view renders BEOWOLF's graveyard
set — dead trees, checkered marble floor, fencing, misty horizon —
through steps and turns, 0 bus errors. (Per the CURSE lesson: the proof
is the OVERRIDE-loaded trace, not the unfamiliar art.)

Caught by the same run: `BIGP0245.TLB` converts to **`bigp0245.ctl`** —
`mac_name()` lowercases the 8.3 stem but does not EXPAND it to the name
the engine probes (`bigpic0245.ctl`). The wall prefix is special-cased;
the clipped picture stems (`BIGP` → `bigpic`, and audit the others) are
not yet. Moot for GEMDOS volumes until the 8.3 item below is resolved —
fix them together.

### ✅ The 8.3 stem audit + ADR-0013 fallback probe (2026-07-17)

Every per-id probe site in the engine, audited:

| family | engine probe | len | DOS 8.3 file | rule |
|---|---|---|---|---|
| walls | `8x8d{b,c}<g><nnn>.ctl` (L6eea) | 9 | `8X8D<g><nnn>.TLB` | letter: `(id<10)?b:c` |
| big pics | `bigpi{c,x}<d><nnn>.ctl` (L579e) | 10 | `BIGP<d><nnn>.TLB` | letter: `(id<248)?c:x` |
| sprites | `SPRIT<d><nnn>.ctl` (L541a) | 9 | `SPRI<d><nnn>.TLB` | expand |
| backdrops | `back<g><nnn>.ctl` | 8 | `Back<g><nnn>.TLB` | identity |
| pictures | `PIC[A-F]1<nnn>.ctl` (L541a) | 8 | same | identity |
| portraits | `CPIC1<nnn>.ctl` (jt56) | 8 | same | identity |

Every 8.3 spelling is the SAME uniform clip — `base[:4] + digit + id:03`
— so the engine now retries exactly that when the derived name misses
(**ADR-0013**, `l33ac`). Both spellings missing still falls back to the
base library. This also fixes **real Mac fan modules**: they ship 8.3
stems too (`BIGP0244.CTL`, `SPRI0052.CTL` — Yezukriis).

**★ THE CLIP-COLLISION TRAP — why 8.3 output is now the converter
DEFAULT.** The expanded names are not merely unshippable on FAT. On the
Hatari GEMDOS mount, the probe AND the host filenames are both clipped
to 8 chars before matching — so `8x8db1001/1003/1005/1008/1009.ctl` all
collapse onto `8x8db100.ctl` and the engine silently opens the FIRST
match: **the wrong wall set, no error**. Measured live: with expanded
names staged, BEOWOLF's tavern start rendered the graveyard set; with
8.3 names + the ADR-0013 probe, the same cell renders its wooden tavern
walls from the exact-id file. (This partially corrects the play-test
above: its `OVERRIDE loaded` traces were real, but the long-name opens
were collision-eligible — the loaded CONTENT could be a neighbouring
id.) `tools/art_convert.py` therefore emits 8.3-verbatim names by
default; `--mac-names` restores the expanded spelling for real Mac FRUA
(HFS keeps long names).

### ✅ Mono `.tlb` synthesis (2026-07-17, 813684b)

The converter now derives a 1-bit GLIB for every converted file and
writes it OVER the HLIB original (same probe name — the mono engine used
to open that HLIB and read garbage into the pool). Ground-truthed
against the base game's own `.CTL`/`.TLB` pairs: per-family scale
factors (walls / BACK / PIC[A-F] ×2, CPIC / SPRIT ×4/3, BIGPIC ×3/2),
mono item modes (`0x90` plain, `0x91` keep-mask+data, `0x92` PackBits,
`0x98` the canonical 16-colour palette item), and the engine's
`g_dsp_ink` luminance class with Bayer 4×4 ordered dithering. SPRIT
approximates the base's mode-3 stream as mask+data (untested in combat).
Verified live: BEOWOLF on ST High loads all three wall groups from its
synthesized `.tlb` overrides (the ADR-0013 "(8.3)" probe; digit-1 mono
names per L6eea) and walks a coherent dithered 1-bit tavern.
Best-effort by construction — the Mac's own 1-bit art was authored, not
derived. `--no-mono` skips; convert a staged copy, the HLIB original is
consumed.

### ✅ Third module, full pipeline — GIANTS' brown title card, at last (2026-07-17)

AGAINST THE GIANTS through the complete pipeline: **79/79** art files
convert (the four BIGP big pictures the RLE work unblocked, and the one
SPRI sprite) and **79/79** synthesize mono twins. On Falcon,
`ART: DESIGN OVERRIDE loaded (8.3): GIANTS.DSN:bigp0245.ctl` — and the
intro shows **GIANTS' own blackletter-on-parchment title card**, the
very asset this document's "magenta intro picture" trap was written
about; it had never rendered before. CPIC/PIC families resolve on the
primary probe; the wall probes correctly fall back to base (GIANTS ships
no wall art). On ST High the synthesized `bigp0245.tlb` loads and
renders — mostly dark, which is FAITHFUL: the card's dominant browns sit
at luminance 65–90, below the engine's own 112 ink threshold, so bright
lettering on near-black parchment is what this art IS in 1-bit; the
following event picture (lighter overland content) dithers richly.
Zero errors in either target.

**Still open (converter follow-ups):**
- ~~**GEMDOS 8.3 on the Atari target**~~ — **CLOSED by ADR-0013** (the
  fallback probe + 8.3-default converter output; see the audit section
  above — including why the long names were a live collision hazard, not
  just a FAT limitation).

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

## ✅✅ POOL OF RADIANCE play-through (2026-07-14) — PLAYS, on its OWN art

Re-run after the `l33ac` override fix. **Zero bus errors, zero pool failures, zero
disk errors across the whole session.**

| | |
|---|---|
| Design loads | ✅ GAME 39 POOL OF RADIANCE |
| **POR's OWN custom art** | ✅ Rolf on the docks, the **temple priest**, the campfire, Phlan's street walls — loaded from its per-id overrides (the path that was broken until today) |
| Intro event chain (Rolf's tour) | ✅ |
| 3D walk, ~80 steps | ✅ view + compass update |
| AREA map | ✅ Phlan's street grid + party marker |
| **TEMPLE** (`jt933`) | ✅ POR's priest; HEAL / DONATE / VIEW / POOL / LEAVE + the services submenu |
| **ENCAMP** | ✅ campfire art, "THE PARTY MAKES CAMP..." |
| **SAVE** | ✅ slot picker A–J; `SavGamB.csv` written (10,284 B) |
| **LOAD** | ✅ picker correctly lists only the saves that exist (A, B); party restored |

### Open, found by playing

- **LOAD from camp drops to the TRAINING HALL** instead of resuming in the dungeon.
  The party restores correctly; the in-dungeon position does not. This is the known
  **L143e design-reload** gap ([[next-session-saveload]]), now confirmed live.
- **The in-game CLOCK does not advance while walking** (12:05 AM held for ~80 steps).
  **NOT POR-specific — HEIRS does the same**, so it is a general step-time gap, not a
  fan-module issue. Do not chase it as a POR bug.
- **No combat reached.** Phlan's civilized quarter is safe *by design*; the slums are
  through the gate and I did not get there. Combat is verified on HEIRS
  ([[combat-encounter-gateway]]) but remains **untested in this module**.

*(The 2026-07-13 run below is superseded; kept for the DISK READ ERROR history.)*

## (superseded) POOL OF RADIANCE play-test (2026-07-13) — it RUNS, with its own art

A **DOS/PC module**, converted with `tools/art_convert.py` and staged as
`POR.DSN`. Driven live in Hatari, start to walkabout:

| | |
|---|---|
| Design loads, appears in SELECT A DESIGN | ✅ "GAME 39 POOL OF RADIANCE" |
| **POR's OWN custom art renders** | ✅ Rolf's portrait (the Council's guide), Phlan's street walls, its area map — correct colours |
| Intro event chain | ✅ plays through (Rolf's greeting → the tour) |
| 3D walk (arrow keys) | ✅ view + compass update, ~70 steps |
| AREA map | ✅ POR's street grid + party marker |
| Play HUD (roster / clock / command bar) | ✅ complete |
| Bus errors | ✅ **zero**, whole session |

This is the first end-to-end proof that **a converted PC module plays with its own
art** — the converter's payoff, and the counterpart to the silent-fallback trap
above. (`-DFRUA_ARTTRACE` confirms the per-id probes fall back and `8x8db.ctl`
then resolves **design-first** to POR's converted library, per ADR-0011.)

### ⚠️ OPEN — "DISK READ ERROR", seen ONCE, NOT reproduced

On the *first* walk a `DISK READ ERROR` plate appeared at the bottom of the play
screen (`l157c(2,…)` — the `jt987` loader's open/callback-failed path). It was
**non-fatal**: the view kept rendering and the walk continued. **Three later runs
over the same ground did not reproduce it**, so it is path- or state-dependent.

**It is recorded as an OBSERVATION, not a diagnosis.** I have not root-caused it
and will not guess: the FAR-pool ceiling (POR's wall libs are 297K + 231K against
a 450K pool) is a *suspect*, not a finding — the faithful `jt987`/`jt104` path
extracts only the ~37K per-set sub-GLIB, so the whole-file sizes may be irrelevant.
Reproduce it before theorising.

### Not established

- **Combat in POR** — no encounter fired in ~70 steps of Phlan. Combat is verified
  working in HEIRS ([[combat-encounter-gateway]]); it is *untested in this module*.
- **A full play-through.** It is playable; it has not been played through.
- The in-game **clock did not advance while walking** (12:05 AM held for ~70 steps).
  This may be entirely faithful — per-step minutes are a per-zone design field
  (GEODATA offset 70). **Do not "fix" it without checking POR's zone data first.**
- **Custom music** (`.xmi` — POR ships `addq1.xmi`) is still ignored; a new
  subsystem, not a lift.

## ✅✅ SOLVED: the DISK READ ERROR was a MIS-LIFT in l33ac's override path

**`l33ac` loaded the design's per-id override into the pool group AND THEN loaded
the BASE library on top of it.** Two libraries in one group; `l4010`'s
`*(long*)(hdr+4) != size` guard fired ("Invalid library header"), `jt104` rejected,
and the engine faithfully reported a DISK READ ERROR and asked for disk 4.

The Mac never does that — a successful override read IS the library, and it RETURNS:

    3556: jsr JT[398]     ; open <design>:<base><digit><nnn>.ctl
    3560: tstw %d0
    3562: blts L358e      ; no override -> fall through to jt987
    356c: jsr JT[460]     ; read it into the pool
    3572: tstb %d0        ; <-- TEST THE RETURN VALUE
    3574: beqs L3584      ; read failed -> close, fall through to jt987
    357a: jsr JT[411]     ; close
    3580: braw L3618      ; *** RETURN. jt987 IS NOT CALLED. ***

The port ignored `jt460`'s result and always fell through. **It never fired on
HEIRS because HEIRS ships NO per-id overrides** — every open missed, and the
fall-through was correct by accident. Pool of Radiance ships them, so it tripped on
the first picture.

**Fixed. POR now loads its OWN override art** (its temple priest renders from
`picb*.ctl`), 0 pool failures, 0 rejects, 0 bad headers, 0 bus errors. HEIRS
unaffected (0 overrides, 0 failures).

★ **This is why the bug wore three disguises.** "Missing art", "missing picture ID"
and "pool exhaustion" were all *downstream of a group we had corrupted*. Each theory
explained the symptom; none was the cause.

## ⛔ RETRACTED: "the DISK READ ERROR is FAR-POOL EXHAUSTION" — WRONG (3rd wrong root cause)

**It is NOT byte exhaustion.** After the GLIB pool flip landed (below), the pool holds
~200 KB of its 450 KB and an 8,148-byte load STILL fails. The real failure, from a
deeper trace:

    RES:   jt1016: l4010 COMMIT failed, size=0

`jt1016` does `jt460` (read) → `size = jt459(groupid)` → `l4010(groupid, 0, size)`
(lift verified EXACT against CODE 5+0x3640). **`jt459(group)` returns 0** — the group
reads as UNBOUND — so `l4010` hits its `*(long*)(hdr+4) != size` guard, logs
"Invalid library header", returns −1, and the load fails. A GROUP-BINDING bug, not a
memory-pressure one.

I inferred "the pool is full" from *"an 8 KB load fails while 8.6 MB of system RAM is
free"* and **never verified the pool was actually full.** It wasn't. Dump the pool's
residents before claiming exhaustion — `-DFRUA_ARTTRACE` now does exactly that.

*(The original reasoning is kept below because the pool flip it motivated is real and
correct on its own merits — just not the fix for this bug.)*

## (superseded) the exhaustion theory

Reproduced in POR and traced to the byte. `-DFRUA_ARTTRACE` now instruments the
resource loader (`l17e2`) and the GLIB load callback (`jt104`):

    RES: jt104 REJECT — jt1016 POOL LOAD FAILED, size=8148
    RES:   group=4
    RES:   FreeMem=8618768
    RES: callback REJECTED  spec=:DISK4:PICB.ctl

**An 8 KB picture could not be loaded while the machine had 8.6 MB free.** It is
not system memory and it is not a missing file — **the 450 KB FAR pool is full.**

The chain, end to end:

1. `jt1016` (load into the FAR pool) fails — no room.
2. `jt104` (the GLIB load callback) therefore returns 0.
3. `l17e2` reads that as a failed load → `l157c(2,…)` = **"DISK READ ERROR"**.
4. `jt987` retries, showing `l157c(1,…)` = **"PLEASE INSERT DISK 4"** — the Mac's
   floppy-swap prompt, faithfully doing its job for a resource it cannot satisfy.

**The 450 KB cap is FAITHFUL and correct** (CODE 6+0x5fc pushes `#450`). What is
wrong is what we PUT in it: **the port loads WHOLE wall libraries into the pool**
(POR's are 296,922 + 231,434 = **528 KB, already over the cap**), where the Mac
extracts only the **requested per-set sub-GLIB (~37 KB)**. `cw_wallfile_load`'s own
comment admits it — *"FALLBACK (migration scaffold, NOT the end state): if the pool
can't hold the ~296K library…"*. POR's start level needs both wall files plus an
event picture and blows the budget immediately; HEIRS mostly escapes it.

**The fix is the pending GLIB pool flip** — make the wall load faithful (per-set
sub-GLIB via `l33ac`/`jt987`/`jt104`), not a whole-file read. It was deprioritised
as "not current priority"; it is now the thing standing between the port and a
playable Pool of Radiance.

### ⛔ Two WRONG root causes I published on the way — and how each died

1. **"Missing converted art."** The mixed-case `.tlb` files (38 of them — `Pica1003
   .tlb`, not `PICA1003.TLB`) really were missed by a case-sensitive `*.TLB` glob,
   and they really do use unsupported drawing methods. It was a tidy story.
   **Killed by staging the NATIVE MAC MODULE** (`POR.sit`, 193 native `.CTL`, zero
   conversion): it fails **identically**. Missing art was never the cause.
2. **"`jt1013` can't find the picture ID."** Plausible from reading `jt104`.
   **Killed by instrumenting it** — that branch never fired.

Both would have been "obvious" and both were wrong. The trace settled it.

### Still open (real, but NOT the disk error)

- **Drawing method 23** (compressed transparent) does not convert. The MAC/CTL
  layout is **solved and confirmed** (`m23_decode`, `planar=False`, consumes SSI's
  own streams exactly); the **DOS sweep layout is NOT** — the row set and opaque
  pixel count come out exact, only the columns land wrong. Ground truth is staged
  (`data/work/fanmods/pormac`). Solve it against that, not against DRAW23.TXT.
- **Method 25** (image-ID list) and the CBODY/COMSPR entry class.
- `art_convert`'s CLI is driven by a shell glob: use `*.TLB *.tlb`, or the
  mixed-case files are silently skipped.

## ⚠️ POR: no combat in the start area — and I did NOT reach the slums

Mapped POR's start area (**GEO016, civilized Phlan**) exhaustively with an extended
`-DFRUA_CELLSCAN` (it now prints each cell's **event TYPE**, read from the 20-byte
event record at `-13038`, `ev[0]` — the same byte `l709e` switches on):

| type | count | what |
|---|---|---|
| 2 | 18 | text / shop-keeper greetings |
| 9 | 8 | **temple** ("The priests of Tyr welcome you") |
| **11** | **2** | **AREA TRANSFER** — `case 11` calls `l5676`, the SAME function as type 5 (stairs) |
| 32 | 1 | select-member-by-class |
| **1 / 33** | **0** | **NO COMBAT AT ALL** |

**Phlan's civilized quarter is safe BY DESIGN.** The two ways out are the type-11
cells at step-coords **(row 4, col 19)** and **(row 5, col 10)**.

**Verified working in POR, on its OWN custom art:** the temple (Priests of Tyr), a
**shop** (the shopkeeper offers items — POR's own portrait), walking, the area map,
LOOK (+10 min), save/load.

**NOT achieved: I did not reach the slums and did not get POR into combat.** I
walled myself in at (6,8) trying to reach the (5,10) transfer. Combat is verified on
HEIRS ([[combat-encounter-gateway]]) and remains **untested in this module**.

### ★ Navigating a design that hides its coordinates

POR hides the coordinate box (a **faithful** per-level flag — `ds[7]` bit 0; see
`docs/…` and do NOT "fix" it), so you cannot steer by the HUD. `FRUA_CELLSCAN` now
also logs **`STEP row*100+col` + the cell's special on every step**, which is the
only practical way to navigate such a design.

⚠️ **The two logs use OPPOSITE row/col order** — the cell scan iterates column-major
(`x` = row, `y` = col) while the step log prints `(-12287, -12288)`. Cross-reference
by SWAPPING. The temple `sp=19` prints as scan `(11,3)` and step `(3,11)`; that pair
is the Rosetta stone.

⚠️ **An event cell RE-FIRES while you stand on it** — leaving the temple menu drops
you back on its cell and the event runs again. Step OFF immediately, or you loop.

## POR play-through, 2026-07-14 — the slums, and three bugs

Drove a real route through *Pool of Radiance* with one level-10 fighter
(GROG), steering by the `-DFRUA_CELLSCAN` step log because **POR hides its
coordinate box** (a faithful per-level flag, `ds[7]` bit 0 — do NOT "fix" it).

**The route out of Phlan.** Civilized Phlan (GEO016) has NO combat cell: 18
text, 8 temple, 1 select-by-class, and two type-11 transfers. The transfer at
step-coords (col 5, row 10) is a *dead end by design* — the Bishop's guards
turn you away, and FORCE PAST just brings the city watch ("you retreat rather
than battle them"). The real exit is the gate at **(col 4, row 19)**: walk
INTO it and `l5676` loads **GEO015 — the slums**, landing you at (col 5, row 1).

**Finding combat.** The slums' combat is not on a cell — it is reached by
CHAIN. `tools`-free recipe: event records live at **offset 3786 of geoNNN.dat,
20 bytes each, 100 slots** (hackdocs `GEOEVENT.TXT`), byte 0 = type. The chain
cell at **(col 10, row 13)** runs QuestStage -> Q-Button ("WHAT DO YOU DO?
LEAVE / TALK / ATTACK") -> ATTACK -> "AN ARMY OF HUMANOIDS APPEARS TO DEFEND
THE OLD WIZARD!" -> combat, 13 humanoids on POR's own sprites.

### Bug 1 — an event's OWN verb bar froze over the play screen (FIXED, fead0b9)

Walk off Tyr's temple and the command bar still read HEAL / DONATE / VIEW /
POOL / LEAVE; decline the transfer and a dead RETURN button survived every
key and click. `l63c0`'s rebuild hook (`g_event_modal_shown`) was armed ONLY
by `l1806`, so an event carrying its own verb bar (temple `l216a`, `l5676`'s
`jt159` question) never triggered it. The Mac exits the command loop after
EVERY event and rebuilds the play dialog from scratch; arm the flag for any
dispatched event. `-DFRUA_BARTRACE` traces the flag across each hop.

### Bug 2 — three stray editor plates + a blank clock on the AREA map — ✅ FIXED

The four shape-5 bevel-frame DLItems are built by `jt240` (the EDITOR's
design-walk driver the port reuses for play; the Mac play walk never runs
jt240) under a `-12290 == 0` 3D-leg guard. `l40f8_area_cmd` flips the leg
AFTERWARDS, so they linger and paint across the top of the street grid. The
clock/coord panel also goes blank on the map. **Pre-existing, not a
regression** (reproduced on the parent commit). Forcing a `play_screen_relayout`
from `l40f8` does NOT clear them — so the plates are reaching the screen by
some path other than the relayout's guarded rebuild. Root cause still open;
don't re-try the relayout hypothesis without new evidence.

### Bug 3 — POR combat enters and renders, but the turn never starts — ✅ FIXED (d3e1999)

**Symptom (as first filed):** the tactical map composes correctly (GROG + 13
humanoids, POR's own combat sprites, floor/wall tiles) — but the right-hand
combat panel stays blank, no combat command bar appears, and neither Return nor
the arrow keys start the turn.

**Root cause: a NULL-target BUS ERROR in the MONSTER turn (`l5b9a` L5d08).** The
monsters win initiative in POR and act first, so the very first turn of the fight
faulted and the whole round loop died with it. `l5b9a` derefs the monster's
target pointer with no NULL check — **and so does the Mac** — which is legal
there and a bus error on the Atari. Full write-up in the "SOLVED" section below;
the standing lesson is in `mac-legal-deref-is-atari-buserror`.

Two guesses in this original entry were WRONG and are corrected below, so don't
act on them: it was **not** POR-specific (HEIRS would fault the same way — its
combat had simply only ever been driven through the PLAYER's turn), and it was
**not** the chained-combat entry. Combat now runs its full lifecycle; see "The
fight, finished".

### The fight, finished (2026-07-14)

With the l5b9a NULL guard in, the combat runs its full lifecycle on POR:

  entry -> 3 ROUNDS -> 112 actor turns -> party defeat -> teardown -> dungeon

The monsters take real turns: they close, they swing, and they CAST — a
lightning bolt arcs across the field and a fireball detonates, both animated on
POR's own art. GROG (level 10, no weapon, alone) was worn down 74 -> 67 -> 9 and
then killed: **"THE MONSTERS REJOICE, FOR THE PARTY HAS BEEN DESTROYED!"** The
post-combat teardown is clean — back to the dungeon, command bar restored, and
the clock advanced 12:05 -> 12:08 AM (combat rounds consume game time; faithful,
and one of the few things that DOES advance the clock — see the JT[914] note).

The player turn works through the command bar: AIM / GUARD / QUICK / DELAY /
VIEW / SPEED / END. **QUICK (auto-combat) resolves the fight** and is the
reliable way to drive a fight headlessly.

Two loose ends, both UNCONFIRMED (they may be input-harness limits, not engine
gaps — verify before chasing):

- **AIM's targeting sub-bar (NEXT / PREV / MANUAL / CENTER / EXIT) did not
  respond** to mouse clicks: "RANGE = 0" never changed and Return did not fire
  the shot. Either the target cursor isn't being driven, or the sub-mode wants a
  key the harness isn't sending.
- **An arrow key on the player turn ended the turn as GUARDING** instead of a
  move/attack. GROG was surrounded with 0 movement left at the time, so this is
  inconclusive — retest with room to step. l08b4 maps arrows to 129..136 ->
  l1162 (move/attack), and both are lifted.

### The two loose ends, RESOLVED — the player turn works; my input was wrong

Both "unconfirmed" combat items above were **harness error, not engine gaps**.
The Falcon mouse can be driven from the keyboard in Hatari (**Alt+arrows**, or
Alt+Shift+arrows for one pixel), which is what exposed it.

**1. AIM works, and its sub-bar is KEY-driven — clicks do NOT reach it.**
Open AIM (click the main-bar button, or drive it as a verb) and it correctly
boxes an adjacent enemy, shows **RANGE = 1**, and repaints the info panel with
the TARGET's stats (HOBGOBLIN / HITPOINTS 6 / AC 5 / MACE). A **TARGET** button
then appears in the sub-bar — it is absent when the cursor sits on the caster
himself (RANGE = 0), which is why the first attempt looked dead.

The sub-bar (NEXT / PREV / MANUAL / TARGET / CENTER / EXIT) does **not respond
to mouse clicks at all** — not even hover-then-press with the pointer visibly on
the button. It takes **LETTER keys**: `t` fires TARGET and commits the swing
(AIM closes, the turn is consumed, play advances to the next actor). Same rule
as every other verb bar in this engine — **verb bars are LETTERS ONLY**.

GROG missed. That is not a bug: an unarmed level-10 fighter punches for **1d2**
against a hobgoblin at **AC 5**. The target-cell pixels before/after the swing
differ ONLY by the AIM highlight box — the swing fired, the blow didn't land.

**2. Arrow keys are IGNORED on the combat player turn.** With a monster adjacent
AND movement available, an arrow does nothing at all — the turn does not even
end. (The earlier "GUARDING" was the turn lapsing, not the key.) `l08b4` expects
commands **129..136** (the direction pad) and routes them to `l1162`
(move/attack); both are lifted. So the open question is whether `l0d16`/`jt173`
translates arrow keysyms to 129..136 in the combat dialog the way `jt297` does
for the dungeon walk. **This is the one genuine combat lead left** — but note the
turn is fully playable without it, via the verb bar.

### CORRECTION: arrows are NOT "ignored" — TARGET opens the MOVE/ATTACK sub-mode

Retracting the "one genuine combat lead" from the previous section. Driving the
turn properly (with **Alt+Insert = left click**, the ST keyboard-mouse binding —
Alt+Home is right click) showed the real command flow:

    AIM (a)  ->  NEXT (n) cycles the target  ->  TARGET (t)
             ->  "MOVE/ATTACK, MOVE LEFT = 3"

That prompt is **l56d8's own string**, verbatim from the source. So `t` does not
"fire a shot" — it commits the target and drops the actor into the **MOVE/ATTACK
sub-mode**, which is where the ARROW KEYS live. Arrows doing nothing at the
top-level verb bar is therefore **correct**: movement is not a top-level action.
There is no arrow-key gap to chase. (I have not yet cleanly isolated a single
arrow step inside the sub-mode — combat turns pass faster than the screenshot
poll — but the framing "arrows are ignored / engine gap" was wrong.)

Confirmed along the way:

- **Alt+Insert really does inject a left click** (validated on the mouse-only
  main menu: it pressed PLAY THE GAME). Alt+arrows move the pointer,
  Alt+Shift+arrows one pixel.
- **The AIM sub-bar is genuinely click-DEAF** — now proven with a click mechanism
  that demonstrably works elsewhere on the same screen. `n` cycles the target
  (RANGE 0 -> 1, the panel repaints with the TARGET's stats, and the TARGET
  button appears); clicks on NEXT do nothing at all. **Verb bars are LETTERS.**
- **The combat message log works**: "HOBGOBLIN ATTACKS GROG, HITTING FOR 5 POINTS
  OF DAMAGE" / "HOBGOBLIN ATTACKS GROG AND MISSES". The right panel is the
  blow-by-blow, and it populates correctly — it only looked "blank" earlier
  because nothing had been logged yet.

### ✅ Fourth module — Game39 (Pool of Radiance), and the row-alignment lesson (2026-07-17)

The heavyweight: 191 art files including whole-library hacks (FRAME /
CBODY / COMSPR / TITLE / BACK / 8X8DB / 8X8DC), DUNG/WILD combat tiles,
mixed-case names, and the method-23 animated pictures. **166/191 colour
+ 165 mono** convert (`c0173b8`); the 25 refusals are all pre-existing
research items (21× method-23, 2× method-25 ID lists, CBODY/COMSPR's
unknown 0x00 entry variant). Colour: Rolf's dock portrait and the Phlan
gatehouse render from the design's own art. Mono: the same gatehouse
walks in dithered 1-bit.

**The lesson that cost a crash:** the mono PackBits decoder unpacks PER
ROW (measured: base `BACK.TLB` streams have zero packets crossing a row
boundary), and the synthesizer's whole-bitmap stream desynchronized it —
POR's `Back1004.tlb` was the FIRST synthesized `0x92` item any boot
ever loaded (BEOWOLF/GIANTS never hit one), and the decoder walked
dither bytes as pointers: a 68000 Address Error wedging the ST-mono
walk. Row-aligned encoding fixed it; the test suite now asserts zero
row-crossings. *A synthesis path is only proven when a module actually
loads it.* Also added: DUNG/WILD mono families (24→32, ×4/3, measured)
and degenerate-entry passthrough for the wall masters' oddities.

Unconvertible HLIB files left in a converted design (FRAME.TLB etc.)
resolve harmlessly in these runs (their probes miss or fall back), but
they remain dead weight a future probe could trip on — candidates for a
converter rename-aside (`.hlib`) if one ever bites.
