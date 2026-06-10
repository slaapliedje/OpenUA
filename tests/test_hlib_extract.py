"""Tests for the FRUA DOS HLIB tile decoder (tools/hlib_extract.py)."""
import struct

import pytest

from hlib_extract import HLib, TRANSPARENT


def _planarize(rows, width):
    """Inverse of the decoder: pack a row-major image (list of lists of
    palette indices, 0xFF = transparent) into VGA Mode-X plane order."""
    height = len(rows)
    stride = (width + 3) // 4
    per_plane = stride * height
    body = bytearray([TRANSPARENT]) * (per_plane * 4)
    for y in range(height):
        for x in range(width):
            plane = x & 3
            col = x >> 2
            body[plane * per_plane + y * stride + col] = rows[y][x]
    return bytes(body)


def _build_hlib(palette, tiles):
    """tiles: list of (width, xhot, yhot, flags, rows). Returns HLIB bytes."""
    entries = []
    # entry 0 — palette: 8-byte header + 16 RGB triples.
    pal = bytearray(b"\x01\x00\x00\x00\x10\x00\x00\x18")
    for (r, g, b) in palette:
        pal += bytes((r, g, b))
    entries.append(bytes(pal))
    # tile entries
    for (width, xhot, yhot, flags, rows) in tiles:
        hdr = struct.pack("<HHHH", width, xhot, yhot, flags)
        entries.append(hdr + _planarize(rows, width))

    count = len(entries)
    table_off = 16
    body_off = table_off + (count + 1) * 4
    offsets = []
    cur = body_off
    for e in entries:
        offsets.append(cur)
        cur += len(e)
    offsets.append(cur)                       # EOF
    total = cur

    out = bytearray()
    out += b"HLIB"
    out += struct.pack("<II", total, count)
    out += b"TILE"
    for o in offsets:
        out += struct.pack("<I", o)
    for e in entries:
        out += e
    assert len(out) == total
    return bytes(out)


VGA16 = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xab), (0x00, 0xab, 0x00), (0x00, 0xab, 0xab),
    (0xab, 0x00, 0x00), (0xab, 0x00, 0xab), (0xab, 0x57, 0x00), (0xab, 0xab, 0xab),
    (0x57, 0x57, 0x57), (0x57, 0x57, 0xff), (0x57, 0xff, 0x57), (0x57, 0xff, 0xff),
    (0xff, 0x57, 0x57), (0xff, 0x57, 0xff), (0xff, 0xff, 0x57), (0xff, 0xff, 0xff),
]


def test_header_and_palette():
    blob = _build_hlib(VGA16, [(4, 0, 0, 0x1504, [[0, 1, 2, 3]])])
    lib = HLib(blob)
    assert lib.tag == b"TILE"
    assert lib.count == 2                     # palette + 1 tile
    assert lib.palette[0] == (0, 0, 0)
    assert lib.palette[6] == (0xab, 0x57, 0x00)
    assert lib.palette[15] == (0xff, 0xff, 0xff)


def test_modex_deplanarize_roundtrip():
    """A diagonal with a transparent gap must survive the Mode-X round-trip
    in correct row-major order (the bug that made tiles look like noise)."""
    rows = [
        [0,   0xFF, 0xFF, 0xFF],
        [0xFF, 5,   0xFF, 0xFF],
        [0xFF, 0xFF, 10,  0xFF],
        [0xFF, 0xFF, 0xFF, 15],
    ]
    blob = _build_hlib(VGA16, [(4, 1, 2, 0x1504, rows)])
    tile = HLib(blob).tiles[0]
    assert (tile.width, tile.height) == (4, 4)
    assert tile.hotspot == (1, 2)
    got = [list(tile.pixels[y * 4:(y + 1) * 4]) for y in range(4)]
    assert got == rows


def test_nonmultiple_of_four_width():
    """Width 7 -> stride 2 (last plane column padded); height derives from
    the body length, not width."""
    rows = [[i for i in range(7)] for _ in range(3)]
    blob = _build_hlib(VGA16, [(7, 0, 4, 0x1504, rows)])
    tile = HLib(blob).tiles[0]
    assert (tile.width, tile.height) == (7, 3)
    got = [list(tile.pixels[y * 7:(y + 1) * 7]) for y in range(3)]
    assert got == rows


def test_rejects_bad_magic():
    with pytest.raises(ValueError):
        HLib(b"NOPE" + bytes(64))


def test_16x16_tile_size():
    """A full 16x16 cursor tile occupies 8 header + 256 body bytes."""
    rows = [[(x + y) & 15 for x in range(16)] for y in range(16)]
    blob = _build_hlib(VGA16, [(16, 7, 7, 0x1504, rows)])
    tile = HLib(blob).tiles[0]
    assert (tile.width, tile.height) == (16, 16)
    got = [list(tile.pixels[y * 16:(y + 1) * 16]) for y in range(16)]
    assert got == rows


# --- Mac GLIB (big-endian, 1bpp data+mask) ---

def _mono_planes(rows, width):
    """rows: 'B' black, 'W' white, '.' transparent. Returns (data, mask)
    big-endian 1bpp planes, ceil(width/16) words per row."""
    height = len(rows)
    wpr = (width + 15) // 16
    rowbytes = wpr * 2
    data = bytearray(rowbytes * height)
    mask = bytearray(rowbytes * height)
    for y in range(height):
        for x in range(width):
            word = x >> 4
            bit = 0x8000 >> (x & 15)
            off = y * rowbytes + word * 2
            cell = rows[y][x]
            if cell != ".":
                mask[off] |= (bit >> 8) & 0xFF
                mask[off + 1] |= bit & 0xFF
                if cell == "B":
                    data[off] |= (bit >> 8) & 0xFF
                    data[off + 1] |= bit & 0xFF
    return bytes(data), bytes(mask)


def _build_glib(palette, tiles):
    """tiles: list of (width, xhot, yhot, flags, rows[strings]). Big-endian."""
    entries = []
    pal = bytearray(b"\x00\x00\x00\x00\x00\x00\x00\x98")
    for (r, g, b) in palette:
        pal += bytes((r, g, b))
    entries.append(bytes(pal))
    for (width, xhot, yhot, flags, rows) in tiles:
        hdr = struct.pack(">HHHH", width, xhot, yhot, flags)
        data, mask = _mono_planes(rows, width)
        entries.append(hdr + data + mask)

    count = len(entries)
    body_off = 16 + (count + 1) * 4
    offsets, cur = [], body_off
    for e in entries:
        offsets.append(cur)
        cur += len(e)
    offsets.append(cur)

    out = bytearray(b"GLIB")
    out += struct.pack(">I", cur)
    out += struct.pack(">HH", count, 0)
    out += b"TILE"
    for o in offsets:
        out += struct.pack(">I", o)
    for e in entries:
        out += e
    return bytes(out)


def test_glib_detect_and_palette():
    blob = _build_glib(VGA16, [(8, 0, 0, 0x0291, ["B." * 4])])
    lib = HLib(blob)
    assert lib.kind == "GLIB"
    assert lib.count == 2
    assert lib.palette[0] == (0, 0, 0)


def test_glib_mono_data_mask():
    """data bit -> black(0), clear-with-mask -> white(15), no mask -> 0xFF."""
    rows = [
        "B...............",   # black, then transparent
        ".W..............",   # white
        "..B.............",
        "...............W",
    ]
    blob = _build_glib(VGA16, [(16, 3, 4, 0x0291, rows)])
    tile = HLib(blob).tiles[0]
    assert (tile.width, tile.height) == (16, 4)
    assert tile.hotspot == (3, 4)
    g = [list(tile.pixels[y * 16:(y + 1) * 16]) for y in range(4)]
    assert g[0][0] == 0 and g[0][1] == 0xFF          # black, transparent
    assert g[1][1] == 15                              # white
    assert g[2][2] == 0                               # black
    assert g[3][15] == 15                             # white at far edge


def test_both_magics_share_reader():
    h = HLib(_build_hlib(VGA16, [(4, 0, 0, 0x1504, [[0, 1, 2, 3]])]))
    g = HLib(_build_glib(VGA16, [(8, 0, 0, 0x0291, ["BWBWBWBW"])]))
    assert h.kind == "HLIB" and h.endian == "<"
    assert g.kind == "GLIB" and g.endian == ">"
