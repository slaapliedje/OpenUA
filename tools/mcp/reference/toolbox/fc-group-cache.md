# FRUA FC — the file cache & GLIB group system (CODE 3)

FRUA's own data-file manager, built ON TOP of the Mac Memory/Resource Manager
(`memory-manager`, `resource-manager`). The game's art and data libraries are
NOT classic resources — they are **GLIB** ("Graphics LIBrary") container files
(`.CTL` / `.TLB` / `.GLB`) loaded on demand into a large pooled buffer, keyed by
a logical **group** tag. This file documents the cache so a lift stays faithful.

Lifted into `src/engine/boot.c` (the FC/FAR pool — `glib_pool_open` /
`glib_pool_close` / `glib_pool_selftest`, the `JT[465]` 14-byte-record family,
and the whole GLIB blit chain) with the pool lifecycle wired from
`src/engine/master.c`. An earlier standalone `src/engine/fc.c` was folded into
`boot.c` — `sym_search` it as `glib_pool_*`, not `fc_*`. Status: the cache +
loader routing is COMPLETE (task #127). Use `fc_groups`, `jt_lookup name=jt468`,
and `sym_search query=glib_pool_open` for the live code.

## What it caches

A single large allocated buffer holds raw file data. Up to **48 logical
groups** map onto up to 48 de-duplicated **14-byte file records** (a 13-char
filename + bookkeeping). A group tag resolves to a record, a record to a slice
of the buffer, and the slice is a GLIB container.

## A5-world data model (the live globals)

| Global     | Role                                                       |
|------------|------------------------------------------------------------|
| `A5-10270` | `g_fc_buffers[]` — data-buffer pointers                    |
| `A5-10074` | `g_fc_group_table[48]` — group → record index (`0xFF`=free)|
| `A5-10026` | `g_fc_records[48]` — 14-byte file records (13-char name)   |
| `A5-9306`  | `g_fc_record_count`                                        |
| `A5-9304` / `A5-9300` | data-buffer end / size                          |
| `A5-9296` … `A5-9292` | FC runtime cursors                              |

De-duplication: two groups that name the same file share one record (and one
buffer slice). That's the cache's economy.

## API — CODE 3 jump-table entries

Only `FCSetup` is a confirmed original name; the rest are inferred.

| Entry              | Routine     | Role                                    |
|--------------------|-------------|-----------------------------------------|
| `JT[463]` `0x538`  | `FCInit`/`_LBOpen` | allocate the data buffer, reset tables |
| `JT[464]` `0x644`  | `FCSetup`   | register a group → file record          |
| `JT[465]` `0xb7a`  | —           | flush records by key (14-byte record family) |
| `JT[466]` `0x632`  | `FCCleanup` | reset the tables, dispose the buffer    |
| `JT[467]` `0x7d0`  | `fc_read`   | reserve + copy bytes from the buffer    |
| `JT[468]` `0xd1a`  | —           | cached-handle lookup (group → base)     |
| `JT[458]` `0x846`  | —           | diagnostic dump of the group table      |

Lift map (current, in `src/engine/boot.c` + `master.c`): `JT[463]/JT[466]` →
`glib_pool_open` / `glib_pool_close` (the FAR pool lifecycle, wired from
`master.c`); the `JT[465]` record-flush family + `JT[467]` `fc_read` + `JT[468]`
cached-handle lookup are in `boot.c` (the conceptual `fc_init/fc_read/...` names
come from the disasm-era `fc.c` that was folded in). Helpers: `L39d2`=memset,
`L3cfa`/`L3952` copy strings/records, `L3bda` compares a record name, `L366a`
a record/list op. (`docs/decompilation.md`'s "→ src/engine/fc.c" line predates
the fold — trust `sym_search`.)

## Resolving a group at blit time — jt468 / l37aa

```
jt468(group)        // CODE 3 0xd1a — resolve a LOADED GLIB base for `group`
                    //   (g_fc_buffers[ g_fc_group_table[group] ]); the data
                    //   model lookup, NOT a loader. L1282 only VALIDATES a
                    //   group is loaded ("Referencing a non-loaded group").
l37aa(base, idx)    // CODE 5 0x37aa — verify 'GLIB' magic, bounds-check item
                    //   idx, return base + offset[idx]  (the item record)
l2856(handle, size, out8)  // copy item's 8-byte metric header; return entry+8
                           //   (the bitmap)
```

The blit entry points stack on these:
- `l309c` = `JT[999]` (CODE 5 0x309c): pen remap (`jt1135`) → `l2856` item
  metric → back off bearings → hand the bitmap to `l2d4e` (the clip+blit).
- `jt1001(top,left,group,item)` (CODE 5 0x31ac) = `l309c(top,left, jt468(group),
  item)` — the SHALLOW (320×200) path. **Arg order matters**: it's
  `jt468(group)` then blit; the historic bug was an arg-2/3 swap (fixed
  986b4dd).
- `jt995` = the DEEP (640×400) equivalent of `jt1001`.
- `l148a(top,left,style,size)`: `jt1200()==3 ? jt995(...) : jt1001(...)`;
  here `style`=group, `size`=item. It's a GLYPH BLIT, not a font draw.

## Group → file map (residency)

| group | file                | residency | contents                       |
|-------|---------------------|-----------|--------------------------------|
| 0     | ALWAYS.CTL          | resident  | buttons / markers / cursors    |
| 1     | FRAME.CTL           | resident  | frame bevels + play chrome     |
| 24    | MENU.CTL            | purgeable | main-menu command-bar plates   |
| (var) | 8X8DB.CTL/8X8DC.CTL | purgeable | dungeon wall sets              |
| (var) | <design>.GLB / *.TLB| purgeable | per-design art libraries       |

Resident groups stay mapped; the rest are purgeable and loaded on demand into
the FAR pool, an LRU cache keyed by the group tag. This is the FRUA-level
analogue of the Resource Manager's purgeable handles (`memory-manager`): a
purged group is reloaded by re-running the loader, and every blit re-resolves
`l37aa(jt468(group), item)` FRESH so it is **purge-safe** — never cache the
resolved item pointer across an allocation.

## The GLIB container format (what a group's bytes are)

| Offset | Field                                                       |
|--------|-------------------------------------------------------------|
| +0     | `long` magic `'GLIB'` (0x474C4942)                          |
| +4     | item count (`u16` + flags in the DOS HLIB variant)          |
| ...    | per-item byte offsets (the offset table `l37aa` indexes)    |
| item   | 8-byte metric header `[h, ybear, xbear, bpp_w, …, flags]`   |
|        | then the bitmap (`height * (bpp_w*8)` bytes for 8bpp)       |

- UI GLIBs (ALWAYS/FRAME) are **8bpp colour**: one CLUT index per pixel, byte
  `255` = transparent, `width_px = metric[6] * 8`. The flags byte `metric[7]`
  bit 6 (`0x40`) is the COLOUR marker (set on UI items; the 1bpp DUNGCOM dungeon
  tiles CLEAR it). `metric[7] & 15` = codec: 0 opaque, 2 PackBits, 7
  transparency-RLE, 9 composite.
- A GLIB can be a **GLIB-of-GLIBs**: `8X8DB.CTL` holds 10 wall SETS, each a
  sub-GLIB of 48 pieces + an item-0 palette. The GEO header's Wall1/2/3 select
  which set; `l37aa(jt468(binder[0]), wall_set[group])` resolves it per tile.

## DOS ↔ Mac note

The DOS release's `HLIB` is the same container (endian-flipped header; count is
`u16`+flags). Designs are byte-identical across releases. `tools/hlib_extract.py`
reads both — but it misreads UI tiles as width-first 1bpp+mask, so trust the
ENGINE reading (8bpp 255-keyed) over the tool's `WxH`.

## Lifting checklist (faithful FC work)

1. Resolve through `jt468(group)` → never hardcode a buffer address.
2. Re-resolve `l37aa(...)` at each use — items move when the group is purged.
3. Honour residency: groups 0/1 resident, others purgeable via the FAR pool.
4. Keep the 48-record de-dup (same filename ⇒ one record/slice).
5. Translate file errors at the shim boundary to Mac `OSErr`; do not stub to
   `noErr` (CLAUDE.md). Out-of-memory in `fc_make_room` evicts LRU, it does not
   fail silently.

See also: `resource-manager` (the purgeable-handle model this mirrors),
`resource-fork-format` (why the rfork ≠ GLIB), and `jt_lookup` / `sym_search`
for the live `fc.c` + blit code.
