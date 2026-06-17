# Classic Mac resource-fork on-disk format

Byte-level layout of a classic Mac OS resource fork — what `tools/macrsrc.py`
parses and what the `rfork_*` MCP tools surface. All fields are **big-endian**.
Offsets below are relative to the start of the fork. Canonical source: *Inside
Macintosh: More Macintosh Toolbox*, "Resource Manager" appendix.

## Top-level header (16 bytes, at offset 0)

| off | size | field                                   |
|-----|------|-----------------------------------------|
| 0   | 4    | offset to **resource data** (from fork start) |
| 4   | 4    | offset to **resource map**  (from fork start) |
| 8   | 4    | length of resource data                 |
| 12  | 4    | length of resource map                  |

(`macrsrc.ResourceFork.__init__` reads these four as `>IIII`.)

## Resource data section

A concatenation of entries, each:

| size | field                          |
|------|--------------------------------|
| 4    | length of this resource's data |
| n    | the data bytes                 |

A resource's *data offset* (from the map) points at the 4-byte length; the data
follows. So `size = u32(data_off + data_ptr)` and the bytes start at
`data_off + data_ptr + 4` (exactly what macrsrc does).

## Resource map

```
+0   16 bytes  copy of the header (reserved at runtime)
+16  4 bytes   reserved (next map handle)
+20  2 bytes   file attributes
+24  2 bytes   offset to the TYPE LIST   (from map start)
+26  2 bytes   offset to the NAME LIST   (from map start)
```

### Type list (at map + typeListOff)

```
+0   2 bytes   (number of types) - 1          // add 1 for the real count
then per type, 8 bytes:
  +0  4 bytes  ResType (4 chars, e.g. 'CODE')
  +4  2 bytes  (count of this type) - 1        // add 1
  +6  2 bytes  offset to this type's REFERENCE LIST (from the type list start)
```

> The "minus one" encoding bites every hand-decoder: the stored counts are one
> less than the real count. macrsrc adds 1 in both places (`n_types`, `count`).

### Reference list (per type, 12 bytes per resource)

```
+0  2 bytes  resource ID (signed)
+2  2 bytes  offset to the resource's NAME (from name list start), 0xFFFF = none
+4  1 byte   resource attributes (the attrs mask — see resource-manager)
+5  3 bytes  offset to the resource's DATA (from data section start), 24-bit
+8  4 bytes  reserved (runtime handle)
```

macrsrc reads `attrs = blob[ref+4]` and `data_ptr = u32(ref+4) & 0x00FFFFFF`
(the attrs byte is the high byte of that long; masking off leaves the 24-bit
data offset).

### Name list (at map + nameListOff)

Packed Pascal strings: a length byte followed by that many `mac-roman` bytes.
A reference's name offset indexes into this list; `0xFFFF` means unnamed.

## Worked correspondence to macrsrc

```python
data_off, map_off, _dlen, _mlen = unpack('>IIII', blob, 0)
type_list = map_off + u16(map_off + 24)
name_list = map_off + u16(map_off + 26)
n_types   = u16(type_list) + 1
for t in range(n_types):
    base   = type_list + 2 + t*8
    rtype  = blob[base:base+4]            # 'CODE', ...
    count  = u16(base + 4) + 1
    reflist= type_list + u16(base + 6)
    for r in range(count):
        ref      = reflist + r*12
        rid      = s16(ref)              # signed id
        name_off = u16(ref + 2)
        attrs    = blob[ref + 4]
        data_ptr = u32(ref + 4) & 0xFFFFFF
        size     = u32(data_off + data_ptr)
        rdata    = blob[data_off+data_ptr+4 : ...+size]
```

## FRUA specifics

- The application fork `data/work/UnlimitedAdventures.rfork` holds the 23
  `'CODE'` segments (THINK C; `CREL`/`DREL` relocations) that are the lift
  target, plus the usual Toolbox resources (`STR#`/`STRS`, `FONT`/`NFNT`,
  `clut`/`pltt`, `DLOG`/`DITL`, `MENU`, etc.).
- Unpacking the Mac release to *produce* that rfork is documented in
  `docs/mac-release.md` (StuffIt → HFS → DiskDoubler → DDAR). `data/` is
  git-ignored — nothing there is committed.
- FRUA's runtime art is mostly NOT classic resources; it lives in `GLIB`
  containers inside `.CTL`/`.TLB`/`.GLB` files loaded by the **FC file cache**
  (`fc-group-cache`). The container format differs from a resource fork (it has
  its own `'GLIB'` magic + item offset table), but the purgeable load-on-demand
  discipline is the same.

Use `rfork_list` (summary or per-type) and `rfork_get(type,id)` to inspect the
real bytes.
