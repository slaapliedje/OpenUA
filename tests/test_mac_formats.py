"""Tests for the Mac resource formats the compat shim parses.

These tests pin down the byte-layout the C parsers (compat/dialogs.c for
ALRT/DITL, compat/mac_font.c for FONT, compat/menus.c for MENU) rely on.
They build synthetic resources, run them through the same FRSC pack /
extract round-trip the Atari engine uses at runtime, and verify the
bytes survive intact with the field offsets the shim expects.

A green test here doesn't prove the C parser is correct on its own, but
it documents the format invariants and catches regressions in our tooling
(macrsrc.py, rsrcpack.py) — which is what feeds the C parser its bytes.
"""
import struct

from fixtures import build_resource_fork
from macrsrc import ResourceFork
from rsrcpack import build_archive


def _frsc_lookup(blob, want_type, want_id):
    """Pull a single resource's data out of a FRSC archive blob."""
    count, tbl_off = struct.unpack_from(">HI", blob, 6)
    for i in range(count):
        e = tbl_off + i * 16
        rtype = blob[e:e + 4]
        rid, _attrs, off, length = struct.unpack_from(">hHII", blob, e + 4)
        if rtype == want_type and rid == want_id:
            return blob[off:off + length]
    raise KeyError((want_type, want_id))


def _pack_one(rtype, rid, data):
    fork = build_resource_fork([(rtype, rid, "", data)])
    return build_archive(ResourceFork(fork).resources)


# --- ALRT ----------------------------------------------------------------
#
# Inside Macintosh / Toolbox Essentials layout:
#   +0  8 bytes Rect   bounds (top, left, bottom, right)
#   +8  short          DITL id
#   +10 short          alert stages
#
# compat/dialogs.c reads bounds via four BE-16 reads at offset 0..6 and
# the DITL id at offset 8.

def _make_alrt(top, left, bottom, right, ditl_id, stages=0):
    return struct.pack(">hhhh hH", top, left, bottom, right, ditl_id, stages)


def test_alrt_round_trips_with_layout():
    raw = _make_alrt(50, 60, 250, 460, ditl_id=200, stages=0x5555)
    bytes_back = _frsc_lookup(_pack_one("ALRT", 200, raw), b"ALRT", 200)
    assert bytes_back == raw
    # The shim's field reads must extract the same values.
    top    = int.from_bytes(bytes_back[0:2], "big", signed=True)
    left   = int.from_bytes(bytes_back[2:4], "big", signed=True)
    bottom = int.from_bytes(bytes_back[4:6], "big", signed=True)
    right  = int.from_bytes(bytes_back[6:8], "big", signed=True)
    ditl   = int.from_bytes(bytes_back[8:10], "big", signed=True)
    assert (top, left, bottom, right, ditl) == (50, 60, 250, 460, 200)


# --- DITL ----------------------------------------------------------------
#
# DITL layout:
#   +0  short              item count - 1
#   +2  N items, each:
#       +0  long           handle / proc placeholder
#       +4  8 bytes Rect   local rect (relative to dialog top-left)
#       +12 byte           item type (low 7 bits; high bit = disabled)
#       +13 byte           item-data length
#       +14 N bytes        item data (button title, text, ...)
#       pad to even byte
#
# compat/dialogs.c walks items by reading the type byte at +12, the data
# length at +13, then advancing 14 + len (+1 if len is odd) per iteration.

def _make_ditl(items):
    """`items` is a list of (top, left, bot, right, type, data: bytes)."""
    buf = bytearray()
    buf += struct.pack(">h", len(items) - 1)
    for top, left, bot, right, itype, data in items:
        buf += b"\x00\x00\x00\x00"                    # placeholder long
        buf += struct.pack(">hhhh", top, left, bot, right)
        buf += bytes([itype & 0xFF, len(data) & 0xFF])
        buf += data
        if len(data) % 2 == 1:
            buf += b"\x00"
    return bytes(buf)


def _walk_ditl(blob):
    n = int.from_bytes(blob[0:2], "big", signed=True) + 1
    off = 2
    out = []
    for _ in range(n):
        rect = struct.unpack(">hhhh", blob[off + 4:off + 12])
        itype = blob[off + 12]
        ilen  = blob[off + 13]
        data  = bytes(blob[off + 14:off + 14 + ilen])
        out.append((rect, itype, data))
        off += 14 + ilen
        if ilen % 2 == 1:
            off += 1
    return out


def test_ditl_three_items_round_trip():
    raw = _make_ditl([
        (114, 193, 139, 273, 0x04, b"OK"),
        (20, 88, 36, 242,    0x88, b"A disk error occurred!"),
        (52, 30, 100, 254,   0x88,
         b"Your disk may be full, or locked, or it may have some other "),
    ])
    items = _walk_ditl(_frsc_lookup(_pack_one("DITL", 200, raw),
                                    b"DITL", 200))
    assert len(items) == 3
    assert items[0] == ((114, 193, 139, 273), 0x04, b"OK")
    assert items[1][1] == 0x88                       # static text type
    assert items[1][2] == b"A disk error occurred!"
    assert items[2][2].startswith(b"Your disk")


def test_ditl_disabled_bit_in_item_type():
    # High bit of type = disabled item; low 7 bits = real type.
    raw = _make_ditl([(0, 0, 20, 60, 0x84, b"DimOK")])   # 0x84 = disabled button
    items = _walk_ditl(_frsc_lookup(_pack_one("DITL", 9, raw), b"DITL", 9))
    itype = items[0][1]
    assert (itype & 0x7F) == 0x04                   # underlying type is button
    assert (itype & 0x80) != 0                      # disabled flag preserved


def test_ditl_odd_length_pads_to_word_boundary():
    # First item has 5-byte data (odd); the second's rect must still parse
    # at the right offset, i.e. the walker recognises the pad byte.
    raw = _make_ditl([
        (0, 0, 20, 60, 0x04, b"hello"),                  # 5 bytes + 1 pad
        (10, 10, 30, 70, 0x04, b"go"),                   # 2 bytes
    ])
    items = _walk_ditl(_frsc_lookup(_pack_one("DITL", 1, raw), b"DITL", 1))
    assert items[1][2] == b"go"
    assert items[1][0] == (10, 10, 30, 70)


# --- FONT ----------------------------------------------------------------
#
# 26-byte header (compat/mac_font.c reads the same fields):
#   +0   fontType, +2 firstChar, +4 lastChar, +6 widMax, +8 kernMax,
#   +10  nDescent, +12 fRectWidth, +14 fRectHeight, +16 owTLoc,
#   +18  ascent, +20 descent, +22 leading, +24 rowWords
# then strike, then locTable, then owTable.

def _make_font(firstC, lastC, fRH, rowWords, asc, desc, owTLoc, strike=None,
               loc=None, ow=None, ftype=0x9000):
    n = lastC - firstC + 2                          # +1 missing glyph
    if strike is None:
        strike = b"\x00" * (rowWords * 2 * fRH)
    if loc is None:
        loc = b"\x00" * ((n + 1) * 2)
    if ow is None:
        ow = b"\x00" * (n * 2)
    header = struct.pack(">HhhHhhHHHhhhH",
                         ftype, firstC, lastC, 8, 0, -1, 8, fRH,
                         owTLoc, asc, desc, 0, rowWords)
    assert len(header) == 26
    return header + strike + loc + ow


def test_font_header_extraction():
    raw = _make_font(firstC=0x20, lastC=0x7E, fRH=8, rowWords=10, asc=7,
                     desc=1, owTLoc=160)
    raw = _frsc_lookup(_pack_one("FONT", 9, raw), b"FONT", 9)
    fields = struct.unpack(">Hhh", raw[0:6])
    assert fields == (0x9000, 0x20, 0x7E)
    fRH    = int.from_bytes(raw[14:16], "big", signed=True)
    owTLoc = int.from_bytes(raw[16:18], "big", signed=True)
    asc    = int.from_bytes(raw[18:20], "big", signed=True)
    desc   = int.from_bytes(raw[20:22], "big", signed=True)
    rowW   = int.from_bytes(raw[24:26], "big", signed=True)
    assert (fRH, owTLoc, asc, desc, rowW) == (8, 160, 7, 1, 10)


def test_font_strike_locTable_owTable_offsets():
    # 95 printable chars + missing glyph + sentinel = build the layout the
    # parser walks: strike at +26, locTable at +26 + strike_bytes,
    # owTable at +16 + owTLoc*2.
    firstC, lastC, fRH, rowWords = 0x20, 0x7E, 8, 10
    n = lastC - firstC + 2
    strike_bytes = rowWords * 2 * fRH                 # 160
    # owTLoc is in words from the field itself (offset 16). For locTable
    # of 2*(n+1) bytes ending at 26 + strike + 2*(n+1), owTable starts
    # immediately after. (26 + 160 + 2*97) - 16, all / 2.
    locTable_bytes = 2 * (n + 1)
    owTLoc_bytes   = 26 + strike_bytes + locTable_bytes - 16
    owTLoc         = owTLoc_bytes // 2
    raw = _make_font(firstC, lastC, fRH, rowWords, 7, 1, owTLoc)
    raw = _frsc_lookup(_pack_one("FONT", 12, raw), b"FONT", 12)
    # The shim computes the same offsets.
    assert raw[26:26 + strike_bytes] == b"\x00" * strike_bytes
    assert raw[26 + strike_bytes:26 + strike_bytes + locTable_bytes] \
        == b"\x00" * locTable_bytes
    ow_off = 16 + owTLoc * 2
    assert ow_off == 26 + strike_bytes + locTable_bytes
    assert raw[ow_off:ow_off + n * 2] == b"\x00" * (n * 2)


def test_font_strike_bit_addressing():
    # Build a 2-glyph font and place one ink pixel in row 0, column 1 of
    # glyph 'B' (slot 1). The C bit-addressing reads MSB-first within
    # each byte; bit (row*rowBytes*8 + col) of the strike.
    firstC, lastC = ord('A'), ord('B')
    fRH = 1
    rowWords = 1                                      # 16-pixel-wide strike
    # Strike: row 0 only; columns 0..15. Glyph A occupies bits 0..7,
    # glyph B occupies bits 8..15 (locTable says so). Place a 1 at bit 9
    # (row 0, column 1 of B).
    strike = bytes([0b00000000, 0b01000000])          # bit 9 high (MSB-first)
    n = lastC - firstC + 2                            # A, B, missing = 3 slots
    # locTable values are bit-columns: glyph A at 0, B at 8, missing+sentinel
    # both at 16.
    loc = struct.pack(">HHHH", 0, 8, 16, 16)[:2 * (n + 1)]
    # Pad with proper-sized table.
    if len(loc) < 2 * (n + 1):
        loc += b"\x00" * (2 * (n + 1) - len(loc))
    ow = struct.pack(">HHH", 0x0008, 0x0008, 0x0008)[:2 * n]
    owTLoc = (26 + len(strike) + len(loc) - 16) // 2
    raw = _make_font(firstC, lastC, fRH, rowWords, 1, 0, owTLoc,
                     strike=strike, loc=loc, ow=ow)
    raw = _frsc_lookup(_pack_one("FONT", 1, raw), b"FONT", 1)
    # Re-extract the bit at (row=0, col=1) of glyph B and verify it's set.
    locTable = raw[26 + len(strike):26 + len(strike) + len(loc)]
    b_loc = int.from_bytes(locTable[2:4], "big")      # second entry = B
    bit_col = b_loc + 1                                # col 1
    byte_off = 26 + 0 * rowWords * 2 + bit_col // 8
    bit_in_byte = 7 - (bit_col & 7)
    assert (raw[byte_off] >> bit_in_byte) & 1 == 1


# --- MENU ----------------------------------------------------------------
#
# Inside Macintosh / Macintosh Toolbox Essentials layout:
#   +0   short  menuID
#   +2   short  menuWidth   (0; computed at draw time)
#   +4   short  menuHeight  (0; computed at draw time)
#   +6   short  menuProc    (MDEF resource id; 0 = standard)
#   +8   short  padding
#   +10  long   enableFlags (bit 0 = whole menu; bit N = item N)
#   +14  Str255 title
#   then for each item:
#       Str255 text
#       byte   icon
#       byte   keyEquiv
#       byte   mark
#       byte   style
#   terminator: a 0 length byte where the next item's text would be.
#
# compat/menus.c GetMenu walks exactly these fields; the per-item meta-tail
# parser (the '/' Cmd-key, '!' mark, '<' style, '(' disable, '^' icon) sits
# inside menu_push_item and is shared with AppendMenu, so we don't pin the
# meta layout here — just the byte offsets of the MENU resource itself.

def _make_menu(menuID, title, items, enableFlags=0xFFFFFFFF):
    """`items` is a list of (text: str, icon, key, mark, style)."""
    buf = bytearray()
    buf += struct.pack(">hhhhhI", menuID, 0, 0, 0, 0,
                       enableFlags & 0xFFFFFFFF)
    tbytes = title.encode("mac_roman")
    buf += bytes([len(tbytes)]) + tbytes
    for text, icon, key, mark, style in items:
        tb = text.encode("mac_roman")
        buf += bytes([len(tb)]) + tb
        buf += bytes([icon & 0xFF, key & 0xFF, mark & 0xFF, style & 0xFF])
    buf += b"\x00"           # terminator
    return bytes(buf)


def test_menu_header_layout():
    raw = _make_menu(128, "File", [("New", 0, ord('N'), 0, 0)],
                     enableFlags=0xFFFFFFFF)
    raw = _frsc_lookup(_pack_one("MENU", 128, raw), b"MENU", 128)
    menuID = int.from_bytes(raw[0:2], "big", signed=True)
    flags  = int.from_bytes(raw[10:14], "big", signed=False)
    title_len = raw[14]
    title     = raw[15:15 + title_len].decode("mac_roman")
    assert menuID == 128
    assert flags == 0xFFFFFFFF
    assert title == "File"


def test_menu_items_walk_to_terminator():
    items = [
        ("New",  0, ord('N'), 0, 0),
        ("Open", 0, ord('O'), 0, 0),
        ("Quit", 0, ord('Q'), 0, 0),
    ]
    raw = _make_menu(128, "File", items)
    raw = _frsc_lookup(_pack_one("MENU", 128, raw), b"MENU", 128)
    off = 15 + raw[14]
    out = []
    while off < len(raw):
        n = raw[off]
        if n == 0:
            break
        text  = raw[off + 1:off + 1 + n].decode("mac_roman")
        meta  = raw[off + 1 + n:off + 1 + n + 4]
        out.append((text, meta[1]))          # key equiv is byte 1 of meta
        off += 1 + n + 4
    assert out == [("New", ord('N')), ("Open", ord('O')), ("Quit", ord('Q'))]


def test_menu_disabled_bit_in_enable_flags():
    # Item 2 disabled: enableFlags bit 2 clear; bit 0 (whole menu) and bit
    # 1, 3 still set.
    flags = 0xFFFFFFFF & ~(1 << 2)
    raw = _make_menu(128, "File",
                     [("New", 0, 0, 0, 0),
                      ("Open", 0, 0, 0, 0),
                      ("Quit", 0, 0, 0, 0)],
                     enableFlags=flags)
    raw = _frsc_lookup(_pack_one("MENU", 128, raw), b"MENU", 128)
    got = int.from_bytes(raw[10:14], "big", signed=False)
    assert (got & 1) != 0                    # menu enabled
    assert (got & (1 << 1)) != 0             # item 1 (New) enabled
    assert (got & (1 << 2)) == 0             # item 2 (Open) disabled
    assert (got & (1 << 3)) != 0             # item 3 (Quit) enabled
