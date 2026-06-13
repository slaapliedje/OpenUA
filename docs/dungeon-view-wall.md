# Dungeon 3D-view worklist — the "broken mirror" placement bug

Status as of 2026-06-13. The first-person view (jt199 frustum -> l5b42 -> jt200 ->
l309c_tile) renders a *broken mirror*: every wall tile lands in the RIGHT half of
the 88x88 pane, the left half is empty, and the wooden door is split into two
fragments. Target = `data/mac_3d_start_e.png` (a clean symmetric stone corridor,
centred door, sky above), at party row=10 col=8 facing=2/E on HEIRS save A.

## What is RULED OUT (do not re-investigate)

- **Tile library** — `data/work/gamedata/8X8DB.CTL` is BYTE-IDENTICAL to the Mac
  original (`data/frua-mac/joined/Disk4/8X8DB.CTL`, 296414 B). make-gamedata ships
  it verbatim.
- **Tile decode** — wall tiles are `flags=0xc5` => mode 5 = raw 8bpp. `l309c_tile`
  reads `body[r*w+c]` at stride `w = bpp_w*8`, 0xff = transparent. Bodies are clean
  (flat fields + tidy perspective wedges). Verified against the live file.
- **Per-side flip** — tiles 6 and 7 are a PRE-MIRRORED pair (6 = left-opaque wedge,
  7 = right-opaque wedge). The Mac *selects* per side; it never flips at runtime.
  The port's trace uses both (idx6 #17/#19, idx7 #21/#22), so selection is present.
- **The blit itself** — an offline render of the EXACT recorded J200DIFF slots
  (real tile bytes + recorded tx/ty + bearings) reproduces the same broken image
  as the live port. So the port faithfully blits the slots it emits.
- **Palette / CLUT band rebase** — structurally faithful; a colour issue would
  read as wrong colours, not a placement scramble. (User confirmed: not a colour
  scramble, a *texture/placement* scramble.)

## ROOT CAUSE — the view-layout delta table has no left/right x separation

The per-slot screen deltas live in `g_a5_-12240..-12198` (22 words), seeded by a
hardcoded `layout[22]` mon snapshot at `src/engine/boot.c` ~8008. The x-deltas each
pass reads (l5b42 does `left = x + xdelta<<2`, NO per-side sign flip):

| pass            | left x-global | right x-global | values   |
|-----------------|---------------|----------------|----------|
| side near-front | -12220        | -12220         | 4 , 4    |
| side recede     | -12202        | -12202         | 4 , 4    |
| front           | -12218        | -12216         | **3 , 3**|
| band gxB        | -12212        | -12210         | **1 , 1**|

Front and band give left == right, so both walls land at the same x => everything
collapses to one side. A symmetric corridor needs the left passes at a smaller /
negative x and the right at a larger x.

The pass STRUCTURE is faithful to the asm — verified `CODE_07.s` L641a (left side
loop) and L65b2 (right side loop) BOTH read `-12220` and `-12202`, matching the
port. So jt199's logic is fine; only the TABLE VALUES are wrong (the mon snapshot
that overrode the DATA-image values `175/516/...` is incorrect/incomplete).

## NEXT — re-derive the table (task #129; #126 reopened)

1. Find the Mac's view-layout INIT that computes `-12240..-12198` (the "launch-time
   init" the boot.c comment cites). Grep the disasm for an indexed/loop store to
   the a5 window (a direct `movew Dn,%a5@(-122xx)` search came up empty — it writes
   via a pointer/`lea` + loop or BlockMove). Lift it so the table is computed, not
   snapshotted.
2. If no clean init exists, re-capture `-12240..-12198` from the real Mac via the
   BasiliskII mon (CurrentA5 @0x0904; the standing frame), AND determine how L/R
   separation is encoded — either the real values are asymmetric, or l5b42 must
   negate `xdelta` for one side. The DATA-image values `175/516/...` may BE the
   correct large deltas with l5b42's transform doing the work; check that path
   before trusting any small-delta snapshot.
3. VALIDATE against `data/mac_3d_start_e.png` (symmetric corridor) AND an offline
   render of the recorded slots — NEVER against the right-heavy 25-slot trace
   (`docs/mac-blit-trace-heirs-l5-standing.md`). Code-multiset match is NOT
   position match (that error closed #126 prematurely).

## Reproduce

```sh
make EXTRA_CFLAGS=-DFRUA_SKIP_ENTRY_EVENTS frua.prg
make gamedata DSN=HEIRS.DSN          # stages HEIRS save A + symlinks the prg
# run under Hatari -d data/work/gamedata; J200DIFF.TXT (recorded slots) +
# VIEWDIAG.TXT land on the GEMDOS drive. The skip-entry build auto-reaches the
# dungeon render.
```
