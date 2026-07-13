# GLIB colour-range palette subsystem

How FRUA allocates the 256-entry CLUT for GLIB pictures (the bigpic / area
backdrops). Reverse-engineered 2026-06-10. The keystone (`l6e58`, the hardware
write) is lifted; the data-feeder functions (`jt1069`/`jt1066`/`jt1067`) are
mapped here and pending. This is task #105 / the backdrop path in
[[dungeon-hud-chrome-arch]].

## Why it exists

GLIB pictures carry their own colours. With a single shared 256-CLUT, the
engine sub-allocates **colour ranges**: each picture asks for N contiguous CLUT
slots, the allocator finds free ones (or reuses a matching range), and the
picture's pixels are remapped to those indices. The subsystem tracks which
slots are in use, packs them, and commits the result to the hardware CLUT.

## A5 data model

| A5 global | size | what |
|-----------|------|------|
| `-3258` | 12 × 8 bytes | the **range table**. Each 8-byte entry: `+0` long sentinel `0x7fffffff` = empty; `+4/+5/+6/+7` match key / `+6` base index / `+7` count (field use varies by phase). |
| `-3162` | 12 bytes | per-range "active/dirty" flag |
| `-3354` | 12 × 8 bytes | backup copy of the range table |
| `-3386` | 256 bits (32 bytes) | **used-slot bitmap**, 1 bit per CLUT index |
| `-3390` | 256 × 3 bytes | the **current** RGB buffer (live CLUT colours) |
| `-3394` | 256 × 3 bytes | the **compacted/work** RGB buffer |
| `-3150` | byte | palette-dirty flag (set by jt1066, gates jt1067 cycling) |
| `-2347` | byte | "scaled/encounter mode" gate — when 0 the whole subsystem no-ops |
| `-1310/-1312/-1318/-1306/-2574/-2578` | — | Mac GDevice / colour-device / depth / window — **no port analogue** |

## Functions

### l6e58 (CODE 5+0x6e58) — the hardware write — **LIFTED**
`l6e58(start, count, mode, buf)` pushes `count` colours from a 3-byte/colour
buffer to CLUT slots `start..`. Mac drives the Color Manager (SetEntries
0xAA9C + SaveEntries/RestoreEntries + the active GDevice ctTable); the port maps
the RGB write onto `qd_set_palette` (shim CLUT → `VsetRGB` via the display HAL)
and drops the GDevice bookkeeping. Reserves CLUT 0..15, never writes past 255.
**The single hardware-palette boundary of the subsystem.**

### jt1069 (CODE 5+0x71b0, ~329 instr) — range allocate — PENDING
HAL-FREE pure data. Given a source colour block + a target slot window, it:
1. scans the 12 range entries for one whose `[base,base+count)` covers the
   request and whose stored colours still match the destination — sets a "dirty"
   flag (fp@-18) on any mismatch;
2. compares the request's RGB triples against `-3390` (current buffer), marking
   `-3386` used-bits and writing changed triples;
3. if dirty, frees the overlapping ranges (entry → `0x7fffffff`, set `-3386`
   bits, set `-3162`), then allocates fresh slots: find a free range entry
   (`l036a "Out of color ranges!"` if all 12 used), allocate via `l01ae`
   (= jt1083) + `jt1134`, `jt406`-copy 4 key bytes in, set `-3162`.
Deps — **all lifted**: jt406, jt1134, l01ae (=jt1083), l036a.

### jt1066 (CODE 5+0x759a, ~280 instr) — compact + commit — PENDING
HAL-FREE except the final `l6e58`. Phase A: walk the 12 `-3162` flags, back up
active ranges to `-3354` (jt406), clear the flag. Phase B: walk the 256-slot
`-3386` bitmap; for each used run, `jt406`-copy the colours `-3390 → -3394`,
track the min/max touched index (fp@-4/fp@-6). Phase C: if anything changed,
`l6e58(min, max-min+1, 1, -3394 + min*3)` to commit, then set `-3150`. Deps —
all lifted (jt406, l6e58).

### jt1067 (CODE 5+0x7772, ~150 instr) — palette cycle — PENDING
Gated on `-3150` (dirty) and `-2347`. Walks the range table, animates colours
over `jt1134` ticks, reads/writes `-3394`/`-3390`. The colour-cycling effect
(torches, water). Deps lifted; lower priority (cosmetic).

### Commit wrappers — partially lifted
- `jt1017` (=LBIndxType) — **lifted**.
- `jt993` (CODE 5+0x20d0, =TNPalette) — PENDING: the per-picture palette commit
  entry; bottoms out in jt1069 + jt1066.
- `l3eea` (CODE 6+0x3eea, = jt124) — skeleton: the GLIB picture+palette commit;
  the `(void)p` TODO is `jt993(jt468(*p), jt1017(jt468(*p)) != 0)`.

## Lift sequencing (each its own verified commit)

1. **l6e58** ✓ (done) — the HAL keystone.
2. **jt1069** ✓ (a3a1263) — the allocator (pure data; faithful 1:1, destp NULL-guarded).
3. **jt1066** ✓ (328ed01) — compact + commit through l6e58; LIVE (present path).
4. **jt1068 + jt993 + l3eea** ✓ (02e4b41) — DNPInit (buffer alloc) + per-picture
   TNPalette commit; l3eea de-skeletoned and LIVE, so the whole chain runs.
5. **jt1067** ✓ (done) — colour cycling; de-stubbed (live in the L23b4 loop).

**SUBSYSTEM FULLY LIFTED.** Every function is present + faithful; the data ->
hardware path is wired and crash-guarded (NULL buffers, -2347 gate, count/ncopy
clamps, lazy jt1068 init). REMAINING (not lifts): (a) Hatari verification in
scaled/encounter mode — first live exercise; a logic bug would mis-colour, not
crash; (b) wiring the bigpic DRAW alongside the palette in jt23's backdrop arms
(L5822/L579e) to retire port_draw_play_frame — see [[dungeon-hud-chrome-arch]].

### Original sequence (historical)
4. **jt993** + de-skeleton **l3eea** (= jt124) — the picture-commit wrappers.
5. **jt1067** — colour cycling (cosmetic, last).
6. Wire into jt23's backdrop arms (L5822/L579e/L33ac) → the bigpic renders with
   its own palette, retiring the port's reconstruction. See
   [[dungeon-hud-chrome-arch]].

## Why the bigpic needs this (recap)

Per [[dungeon-hud-chrome-arch]]: drawing a GLIB picture without its palette lays
down opaque pixels that render as black garbage (the reverted experiment). The
picture + palette are a package; this subsystem is the palette half, and it must
land before the bigpic backdrop can be drawn at all.


## jt1069 / jt1066 audit (2026-07-13) — both FAITHFUL; the cast is elsewhere

Chased the converted-big-picture colour cast into the allocator. **Neither jt1069
nor jt1066 is at fault.**

### ⚠️ TRAP: jt1069's phase-1 compare looks like a mis-lift. IT IS NOT.

```c
if (e[4] != d[0] && e[5] != d[1] && e[6] != d[2] && e[7] != d[3])
        dirty = 1;
```

`&&` reads like a bug — a "has this changed?" test wants `||`, and this is exactly
the shape of the mis-lifts the JT[3] straddle sweep found. **Check the asm before
touching it.** `CODE_05.s` 0x723e–0x72d6: each `cmpb` is followed by a `beq L72dc`
that jumps *out* to "not dirty", so dirty is set only when **all four** bytes
differ. The `&&` is an exact transcription. Changing it to `||` would be a
gate-flip on correct code.

### What the two functions actually do

Both are lifted faithfully and both are **wired** (jt993 → jt1069 → jt1066).
For a BIG PICTURE the caller passes `ncopy = 0`, `destp = NULL`, so:

- **jt1069** skips range allocation (phase 3b) entirely. It only diffs the incoming
  colours against the LIVE mirror (-3390), marks changed slots in the used-bitmap
  (-3386), and frees overlapping ranges. It never touches hardware.
- **jt1066** promotes the used slots LIVE → WORK (-3394), tracks the touched span
  [min,max], and commits `WORK[min..max]` through `l6e58`. One contiguous write.

Neither can produce a hue shift on its own: they move RGB triples verbatim.

### Where to look next

`l442e` (the big-picture composer) makes **no palette call at all** — so a
picture's palette must reach the CLUT via the resource-load path, not the
composer. Find who calls jt993 for an event/big picture and with which resource
`idx`; if a big picture's palette is never committed (or is committed and then
overwritten by a later load), the picture would blit its 32..255 pixel indices
against **whatever palette was loaded last** — which is precisely the failure mode
UAPALETT.TXT warns about ("UA always uses the most recently loaded palette").
That is the strongest remaining hypothesis and it is untested.


## ★ Who commits a picture's palette — and why a big picture may get NONE

Traced 2026-07-13, chasing the converted-big-picture colour cast.

**The palette lands in TWO steps, and the first one is deliberately BLACK.**

1. **`l3f3c(lo, hi)`** ("JT[105]: install palette", called by `l579e` after a
   bigpic loads) **reserves** the range. It zero-fills a 768-byte scratch, hands
   that to `jt1069`, and commits with `jt1066`:

   ```c
   jt399(buf, 768, 0);                       /* ZERO-fill */
   jt1069(lo, hi - lo + 1, buf, 0, NULL);    /* stage 224 BLACK triples */
   jt1066();
   ```

   **This is FAITHFUL** — verified against `CODE_06.s` 0x3f50–0x3f80. It really
   does seed the range black. It is a *reservation*, not the palette.

2. **`jt124` (= `l3eea`) -> `jt993` (TNPalette) -> `jt1069`/`jt1066`** is what
   actually installs the picture's own RGBs, for a given **group handle**.

**So a picture whose group handle is never passed to `jt124` never gets its
palette installed at all.** It blits its 32..255 indices against whatever is
resident — the classic failure `UAPALETT.TXT` describes ("UA always uses the most
recently loaded palette"). Structurally perfect image, wrong hues. That is exactly
the symptom.

The `jt124` callers each commit a specific handle: `-24260` (the bigpic backdrop,
committed from 4 sites), `-22222`, `-10366`, `-27894 + band*4`, `-24320`.

**⚠️ `l442e` — the event / big-picture composer — makes NO palette call at all.**
No `jt124`, no `jt993`, no `l3f3c`, no `jt1066`.

### PROBE RESULT (2026-07-13) — the ART GALLERY installs NO palette

Ran it: `make ENGINE_PROBE=1` (**note: the probe is gated on `ENGINE_PROBE=1` and
defaults OFF — a probe build is required, or every count reads 0 and you get a
false negative; I hit exactly that first**), then displayed a BIG PICTURE in the
ART GALLERY.

**Every function in the chain fired ZERO times:** `L579e`, `L3f3c`, `jt1069`,
`jt1066`, `jt124`/`L3eea`, `jt993`, `jt1017`. The probe itself was live (1906
calls logged at boot) and the picture demonstrably loaded (`bigpic0245.ctl` in the
conout log). So **the gallery blits a big picture without ever installing its
palette** — it renders against whatever CLUT is resident. That is the cast.

### ⚠️ …but this is the EDITOR path, NOT gameplay — scope corrected

`l442e` (the in-game event-picture composer) **does** reach the chain:
`l442e:41583 -> l579e (JT[43] load bigpic) -> l3f3c -> jt1069/jt1066`. So the
gameplay path IS wired, and my earlier "l442e makes no palette call" note was
looking at the wrong level — it calls `l579e`, which is what installs.

**So the observed cast may be an ART GALLERY (editor) defect only.** UNTESTED:
whether an in-game event picture (e.g. the shop merchant portrait, which goes
through `l442e`) shows the same cast. **Do that next** — probe with `ENGINE_PROBE=1`
while a shop/event picture displays and check whether `L3f3c`/`jt1066` fire. It
decides whether this is a cosmetic editor bug or a real gameplay one, and they
have very different priorities.

**Do NOT "fix" `l3f3c`'s zero-fill.** It looks like a bug and is not; the asm says
zero. Same trap as `jt1069`'s `&&` (above).
