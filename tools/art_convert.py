#!/usr/bin/env python3
"""FRUA art-container converter — DOS `HLIB` <-> Mac `GLIB`.

WHY: fan modules are authored for one release and ship art only in that
release's format.  A PC module's art is doubly unreadable by the Mac-derived
engine: wrong byte order *and* a different pixel layout.  This converts between
them so the ~313 hacked PC modules become usable (see docs/fan-module-hacks.md).

GROUND TRUTH.  The transform below was not inferred from the engine — it was
*derived by diffing the same module shipped in both formats*: "The Curse of
Yezukriis" exists as `pc/modules/y/yezu.zip` (HLIB) and
`mac/modules/y/yezu1mac.zip` (GLIB), nine matched assets, authored once.  Every
rule here reproduces the real Mac bytes **exactly**; `tests/test_art_convert.py`
re-checks that against the real pair when it is staged.

CONTAINER (identical in both, only the byte order differs)

    magic(4)   'HLIB' little-endian | 'GLIB' big-endian
    size(u32)  total file size
    count(u16) number of entries
    flags(u16)
    tag(4)     'TILE' / 'DATA'
    offsets    u32 x (count+1)   -- entry i spans offsets[i]..offsets[i+1]

The offset table is *identical* between the two formats (same values, swapped
bytes) whenever no entry is recompressed, so the file size is unchanged.

ENTRY = 8-byte header + payload

    u16 rows        row count            (byte-swapped)
    i16 xhot, yhot  hot spot             (byte-swapped)
    u8  stride      HLIB: W/4  GLIB: W/8    <-- NOT a plain swap
    u8  flags       low nibble = piece type; high nibble 0x1 (DOS) <-> 0xc (Mac)

`W` is the row width in bytes, padded up to a multiple of 4.  HLIB stores W/4
because that is its Mode-X *per-plane* stride; GLIB stores W/8.

PIXELS (the part a byte-swap alone will never fix)

    HLIB: VGA Mode-X, 4 unchained planes, PLANE-MAJOR — plane p holds the
          columns with x % 4 == p, all rows of plane 0, then all of plane 1, ...
    GLIB: linear rows of W bytes.

Deplanarising HLIB yields the Mac bytes verbatim (12/12 tiles in the Yezukriis
pair, byte-exact).

FORMER LIMITATIONS, NOW CLOSED (stale copies of this header cost real time
--------------------------------------------------------------------------
1. ~~piece type 2 refused~~ -- BOTH DOS codecs are decoded now: drawing
   method 18 (DRAW18.TXT run codec, shared with the Mac) and method 23
   (skip/literal transparent, DRAW23.TXT, a DIFFERENT codec).  See
   `rle_decode` / `m23_decode` below.

2. ~~wall libraries render a BLACK 3D view~~ -- that was the byte[10]/[11]
   swap (two independent bytes, NOT a u16; swapping moved the ID-table
   magic).  Fixed; POR walks Phlan on its own converted walls.

3. ~~the 8X8D wall-set name is ambiguous under DOS 8.3~~ -- the engine's
   wall loader (CODE 7 L6eea) picks the letter from the SET-ID band,
   `(id < 10) ? 'b' : 'c'`; `mac_name()` applies the same rule.

STILL OPEN: no mono `.tlb` synthesis (converted modules play mono with base
1-bit art), and Mac-convention override names exceed GEMDOS 8.3 on real
Atari volumes -- see docs/fan-module-hacks.md "Still open".

CAUTION: the "opaque" fallback in `_convert_entry` (payload copied verbatim when
the geometry does not describe it) is *proven* only for the picture palettes of
the Yezukriis pair.  It is a silent passthrough, and item 2 is what it looks like
when that assumption is wrong.
"""
import os
import struct
import sys

HLIB, GLIB = b"HLIB", b"GLIB"
HDR = 16                      # magic + size + count + flags + tag
# (drawing-method constants live next to _convert_entry)


class UnsupportedPiece(Exception):
    """An entry uses a codec we cannot faithfully convert (see module docs)."""


class BadContainer(Exception):
    pass


def _endian(magic):
    if magic == GLIB:
        return ">"
    if magic == HLIB:
        return "<"
    raise BadContainer("not an HLIB/GLIB container: %r" % magic[:4])


def parse(data):
    """-> dict(magic, endian, count, flags, tag, offsets, entries)."""
    if len(data) < HDR:
        raise BadContainer("truncated: %d bytes" % len(data))
    magic_ = data[:4]
    e = _endian(magic_)
    size, = struct.unpack(e + "I", data[4:8])
    count, = struct.unpack(e + "H", data[8:10])
    # Bytes 10 and 11 are TWO INDEPENDENT BYTES (TLBFORM.TXT): [10] unused,
    # [11] "magic" -- 1 means the file carries an ID table.  They are NOT a u16:
    # byte-swapping them moves the magic into the unused slot.  Picture files
    # have magic=0 so a swap is a harmless no-op there, which is why this hid --
    # but MASTER LIBRARIES (the wall sets) always have magic=1, and corrupting it
    # is what rendered the converted wall art as a BLACK 3D view.
    unused, magic = data[10], data[11]
    tag = data[12:16]
    end = HDR + 4 * (count + 1)
    if end > len(data):
        raise BadContainer("offset table runs past EOF")
    offsets = list(struct.unpack(e + "%dI" % (count + 1), data[HDR:end]))
    entries = [data[offsets[i]:offsets[i + 1]] for i in range(count)]
    return dict(magic=magic_, endian=e, size=size, count=count,
                unused=unused, id_table=magic, tag=tag,
                offsets=offsets, entries=entries)


def _swap_flag_byte(b, to_mac):
    """Entry flags: keep the piece type (low nibble), remap the high nibble.

    Observed across the Yezukriis pair: 0x15<->0xc5, 0x10<->0xc0, 0x12<->0xc2,
    and 0x08<->0x08 (palette / opaque entries, unchanged).  Rather than guess at
    unseen values we refuse them -- a wrong flag byte would render as garbage.
    """
    lo, hi = b & 0x0F, b >> 4
    if hi == 0x0:
        return b                      # palette / opaque: identical in both
    if to_mac and hi == 0x1:
        return 0xC0 | lo
    if not to_mac and hi == 0xC:
        return 0x10 | lo
    raise UnsupportedPiece("unknown entry flag byte 0x%02x" % b)


def deplanarize(px, w, rows):
    """Mode-X (plane-major, 4 unchained planes) -> linear rows of `w` bytes."""
    out = bytearray(w * rows)
    k = 0
    for p in range(4):
        for y in range(rows):
            for x in range(p, w, 4):
                out[y * w + x] = px[k]
                k += 1
    return bytes(out)


def planarize(px, w, rows):
    """Linear rows of `w` bytes -> Mode-X (plane-major).  Inverse of the above."""
    out = bytearray(w * rows)
    k = 0
    for p in range(4):
        for y in range(rows):
            for x in range(p, w, 4):
                out[k] = px[y * w + x]
                k += 1
    return bytes(out)



def rle_decode(data, total):
    """The UA run codec (hackdocs DRAW18.TXT), shared by DOS and Mac.

    x < 128  -> the next x+1 bytes are literal
    x > 128  -> the next byte, repeated 257-x times
    x == 128 -> no-op
    """
    out = bytearray()
    i = 0
    while len(out) < total and i < len(data):
        x = data[i]
        i += 1
        if x < 128:
            out += data[i:i + x + 1]
            i += x + 1
        elif x > 128:
            out += bytes([data[i]]) * (257 - x)
            i += 1
    return bytes(out)


def rle_encode(buf):
    """Encode with the same codec.  Runs of 3+ become repeats (a 2-run costs the
    same as a literal), literals accumulate, and runs flow across row boundaries.

    NOT byte-identical to SSI's own encoder — ours lands ~0.25% larger on the
    Yezukriis BIGPIC.  For a COMPRESSED payload that is fine and the distinction
    matters: the correctness bar is that the decoder reproduces the right PIXELS,
    which `tests` assert, not that we reproduce SSI's exact run splits.  (For the
    uncompressed paths byte-exactness IS achievable, and is asserted.)
    """
    out, lit, i, n = bytearray(), bytearray(), 0, len(buf)

    def flush():
        s = 0
        while s < len(lit):
            chunk = lit[s:s + 128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            s += 128
        del lit[:]

    while i < n:
        j = i
        while j + 1 < n and buf[j + 1] == buf[i] and j - i + 1 < 128:
            j += 1
        run = j - i + 1
        if run >= 3:
            flush()
            out.append(257 - run)
            out.append(buf[i])
            i = j + 1
        else:
            lit.append(buf[i])
            i += 1
    flush()
    return bytes(out)


def _rows_deinterleave(raw, w, rows):
    """DOS stores each ROW as 4 plane sweeps (DRAW18.TXT: "4 sweeps ... completing
    the row").  Note this is PER-ROW — unlike the uncompressed methods, whose
    planes span the WHOLE image."""
    out = bytearray(w * rows)
    k = 0
    for y in range(rows):
        for p in range(4):
            for x in range(p, w, 4):
                out[y * w + x] = raw[k]
                k += 1
    return bytes(out)


def _rows_interleave(lin, w, rows):
    out = bytearray(w * rows)
    k = 0
    for y in range(rows):
        for p in range(4):
            for x in range(p, w, 4):
                out[k] = lin[y * w + x]
                k += 1
    return bytes(out)


# Drawing methods, per the UA Shell hackdocs (TLBFORM.TXT).  This is the last
# byte of an image header -- NOT a "flags" field, as an earlier guess here had it.
DRAW_UNCOMPRESSED = (16, 17, 21)      # 17 carries an AND/OR mask pair
DRAW_COMPRESSED = (18,)               # PackBits-style RLE; DRAW18.TXT
DRAW_TRANSPARENT = 23                 # a DIFFERENT codec entirely; DRAW23.TXT
DRAW_ID_LIST = 25


# --- drawing method 23: compressed TRANSPARENT (DRAW23.TXT) ------------------
#
# NOT the method-18 codec, and lumping the two together (DRAW_COMPRESSED = (18,
# 23)) is what silently broke every animated picture in Pool of Radiance.  A
# method-23 stream is skip/literal, per row:
#
#     v == 0    end of row
#     v >= 128  SKIP (257 - v) pixels -- they stay TRANSPARENT
#     v <  128  the next v bytes are literal pixels
#
# The layouts differ, exactly as the uncompressed paths do:
#   DOS/TLB : 4 plane sweeps.  In sweep p, each row starts at column p and steps
#             by 4 ("advancing the offset on the screen by 4 for each pixel").
#             All rows are walked once per sweep, then it wraps for the next.
#   Mac/CTL : one pass, "each row is plotted completely before going to the next".
#
# Skips are the transparency: a skipped pixel is never written.  So a converter
# has to carry a MASK, not just pixels -- re-encoding without it would paint the
# transparent background opaque.

def m23_decode(payload, w, rows, planar):
    """Method-23 stream -> (pixels, mask).  mask[i] = 1 where a pixel was plotted.

    The DOS (planar) sweep layout — SOLVED 2026-07-18 against the Mac
    edition of Pool of Radiance, whose GAME39.dsn ships the same sprites:

        sweep p starts at x = p + 1;  literal: plot, x += 4;
        skip byte v: x += 4*(256 - v)      [NOT 257 — one of TWO
                                            off-by-ones in DRAW23.TXT]

    Proof: this decode re-ENCODES back to SSI's own DOS bytes EXACTLY for
    57 of POR's 62 method-23 items; the other 5 contain the corpus's 35
    edge pixels at exactly x == W (an encoder artifact one past the
    sprite, clipped here) and re-encode shorter by just those pixels.  It
    is also the ONLY candidate law with ZERO double-written pixels across
    the corpus (every rejected layout family produced thousands).  The
    residual differences against the Mac-edition render are the Mac's own
    re-dithered art, not the codec.  Note x == 0 is unreachable in this
    layout (sweep 3 covers the x % 4 == 0 class from x = 4)."""
    px, mask = bytearray(w * rows), bytearray(w * rows)
    i, n = 0, len(payload)
    for sweep in range(4 if planar else 1):
        for y in range(rows):
            x = sweep + 1 if planar else 0
            while True:
                if i >= n:
                    return px, mask, i          # truncated: caller decides
                v = payload[i]; i += 1
                if v == 0:
                    break                        # end of row
                if v >= 128:
                    if planar:
                        x += 4 * (256 - v)       # transparent skip
                    else:
                        x += 257 - v
                    continue
                for _ in range(v):              # literal run
                    if i >= n:
                        return px, mask, i
                    if 0 <= x < w:
                        px[y * w + x] = payload[i]
                        mask[y * w + x] = 1
                    i += 1
                    x += 4 if planar else 1
    return px, mask, i


def m23_encode(px, mask, w, rows, planar):
    """(pixels, mask) -> a method-23 stream in the requested layout.

    Planar column sets and skip units follow the solved law (see
    m23_decode): sweep p owns x = p+1, p+5, ...; a gap of g columns is a
    skip byte 257 - (g + 1) [decoded as 4*(256 - v) = 4g]."""
    out = bytearray()
    step = 4 if planar else 1
    if not planar:
        # The linear layout CANNOT express a 1-pixel transparent gap
        # (skip n = 257 - v is >= 2 by construction), and SSI's own Mac
        # streams contain none — their pipeline evidently filled them.
        # Do the same: an isolated gap between opaque pixels becomes a
        # literal carrying its left neighbour's value (1px of background
        # lost; the alternative is dropping an opaque pixel).
        px, mask = bytearray(px), bytearray(mask)
        for y in range(rows):
            row = y * w
            xs = [x for x in range(w) if mask[row + x]]
            if not xs:
                continue
            for x in range(xs[0] + 1, xs[-1]):
                if (not mask[row + x] and mask[row + x - 1]
                        and mask[row + x + 1]):
                    mask[row + x] = 1
                    px[row + x] = px[row + x - 1]
            if xs[0] == 1:
                # a LEADING 1-gap is equally inexpressible. SSI faced the
                # same wall: their Mac streams have no first-opaque-at-1
                # rows but 209 first-at-0 rows — a column the DOS layout
                # cannot even produce. Fill x=0 the same way.
                mask[row] = 1
                px[row] = px[row + 1]
    for sweep in range(4 if planar else 1):
        for y in range(rows):
            cols = list(range(sweep + 1, w, step)) if planar else list(range(w))
            row = y * w
            # trailing transparency needs no skip -- just end the row.
            last = -1
            for k, x in enumerate(cols):
                if mask[row + x]:
                    last = k
            k = 0
            while k <= last:
                x = cols[k]
                if not mask[row + x]:
                    run = 0
                    while k + run <= last and not mask[row + cols[k + run]]:
                        run += 1
                    if planar:
                        # v = 256 - g (the solved law); g in 1..128 per byte
                        while run:
                            take = min(run, 128)
                            out.append(256 - take)
                            k += take
                            run -= take
                        continue
                    while run:
                        # 257-v = n, and v must be a byte >= 128  ->  n in 2..129.
                        # A 1-skip is NOT encodable; borrow from the next chunk.
                        take = min(run, 129)
                        if run - take == 1:
                            take -= 1
                        if take < 2:
                            take = min(run, 2)
                        out.append(257 - take)
                        k += take
                        run -= take
                    continue
                run = 0
                while (k + run <= last and mask[row + cols[k + run]]
                       and run < 127):
                    run += 1
                out.append(run)
                for j in range(run):
                    out.append(px[row + cols[k + j]])
                k += run
            out.append(0)                        # end of row
    return bytes(out)
def _is_colour_table(method):
    """A colour-table (palette) block, per the ENGINE's own rule.

    jt993 ("TNPalette") rejects anything else with exactly this test:

        if ((hdr[7] & 15) != 8) { "Invalid TNPalette call ..."; return; }

    The LOW NIBBLE is the block type (8 = palette); the HIGH NIBBLE is the
    format field, and it is remapped DOS<->Mac like any other entry's.  This is
    what the hackdocs' unexplained "value should be either 8 or 24" means -- 24
    is 0x18, i.e. type 8 with format 1 -- and it is why the Mac's 200 (0xc8) is
    equally valid.  No drawing method has low nibble 8 (16/17/18/21/23/25 ->
    0/1/2/5/7/9), so there is no collision.

    Keying off the low nibble rather than a list of observed VALUES is the whole
    point: a hardcoded (8, 24) missed both DOS 0x18 -> Mac 0xc8 and the Mac's own
    0xc8 on the way back.  Use the engine's rule, not a sample.
    """
    return (method & 0x0F) == 8


def _convert_entry(ent, to_mac):
    """Convert one image entry: 8-byte header + data.

    Header (TLBFORM.TXT): u16 height, i16 v-offset, i16 h-offset,
                          u8 width/4, u8 drawing method.
    """
    if len(ent) < 8:
        return ent                                   # empty / degenerate slot
    src = "<" if to_mac else ">"       # CURRENT order: HLIB when heading to Mac
    dst = ">" if to_mac else "<"
    height, voff, hoff = struct.unpack(src + "Hhh", ent[0:6])
    w4, method = ent[6], ent[7]
    payload = ent[8:]

    # TYPE-128 TABLE (CBODY/COMSPR item 0 — the composite-body index).
    # For THIS entry kind bytes [6:8] are ONE u16 (value 0x0080 in the
    # source's endianness), not the usual w4/method pair, and the payload
    # is a u16 array. Measured on the POR pair: header words swap, the
    # [6:8] word swaps, the payload u16-swaps; nothing else. The same
    # table appears unchanged in the base game's mono .TLB twins.
    if (w4, method) == ((0x80, 0x00) if to_mac else (0x00, 0x80)):
        dst_67 = bytes([0x00, 0x80]) if to_mac else bytes([0x80, 0x00])
        return (struct.pack(dst + "Hhh", height, voff, hoff)
                + dst_67 + _swap_u16_array(payload))

    w = w4 * (4 if to_mac else 8)       # decode width with the SOURCE's unit
    dos_method = method if to_mac else (method & 0x0F) | 0x10

    # Classify by DRAWING METHOD first (TLBFORM.TXT).  An entry whose method is
    # not an image method is the per-set COLOUR TABLE, whose trailing byte is a
    # magic of 8 or 24 -- do not read it as a bitmap.
    if dos_method in DRAW_COMPRESSED:
        # Both releases use the SAME run codec; they differ only in pixel order.
        # DOS interleaves 4 plane sweeps per ROW, Mac is linear.  Proved by
        # decoding the Yezukriis BIGP0244 from both sides: identical pixels.
        if not (w and height):
            raise UnsupportedPiece("compressed entry with no geometry")
        if to_mac and w % 8:
            raise UnsupportedPiece("width %d not a multiple of 8" % w)
        raw = rle_decode(payload, w * height)
        if len(raw) != w * height:
            raise UnsupportedPiece(
                "compressed payload decoded to %d bytes, expected %dx%d"
                % (len(raw), w, height))
        lin = _rows_deinterleave(raw, w, height) if to_mac else raw
        body = rle_encode(lin if to_mac else _rows_interleave(lin, w, height))
        return (struct.pack(dst + "Hhh", height, voff, hoff)
                + bytes([w // (8 if to_mac else 4),
                         _swap_flag_byte(method, to_mac)]) + body)
    if dos_method == DRAW_TRANSPARENT:
        # BOTH layouts are solved now.  The DOS sweep law (2026-07-18, see
        # m23_decode) was derived against the Mac edition of Pool of
        # Radiance and PROVEN by re-encoding SSI's own DOS streams
        # byte-exactly (57/62; the rest differ only by clipped x==W edge
        # pixels).  The Mac/CTL linear layout was already proven the same
        # way (615/615 bytes on POR's PICA1003).
        if not (w and height):
            raise UnsupportedPiece("method 23 entry with no geometry")
        if to_mac and w % 8:
            raise UnsupportedPiece("width %d not a multiple of 8" % w)
        px, mask, used = m23_decode(payload, w, height, planar=to_mac)
        if used < len(payload) - 1:     # POR's FRAME.TLB carries 1 pad byte
            raise UnsupportedPiece("method 23 stream consumed %d of %d bytes"
                                   % (used, len(payload)))
        body = m23_encode(px, mask, w, height, planar=not to_mac)
        return (struct.pack(dst + "Hhh", height, voff, hoff)
                + bytes([w // (8 if to_mac else 4),
                         _swap_flag_byte(method, to_mac)]) + body)

    if dos_method == DRAW_ID_LIST:
        # Animation frame-sequence table. Measured on the POR pair
        # (PICA1003 items 6/7): the payload is BYTE-IDENTICAL across
        # releases — only the header re-encodes.
        return (struct.pack(dst + "Hhh", height, voff, hoff)
                + bytes([w // (8 if to_mac else 4),
                         _swap_flag_byte(method, to_mac)]) + payload)

    if dos_method in DRAW_UNCOMPRESSED:
        if w and height and 2 * w * height == len(payload):
            # AND+OR mask pair (CBODY/COMSPR combat bodies): TWO planes,
            # each Mode-X-shuffled independently. Proven byte-exact on
            # the POR pair (halves-deplanarized == the real Mac bytes).
            if to_mac and w % 8:
                raise UnsupportedPiece("width %d not a multiple of 8" % w)
            half = w * height
            fn = deplanarize if to_mac else planarize
            body = fn(payload[:half], w, height) + fn(payload[half:], w, height)
            return (struct.pack(dst + "Hhh", height, voff, hoff)
                    + bytes([w // (8 if to_mac else 4),
                             _swap_flag_byte(method, to_mac)]) + body)
        if not (w and height and w * height == len(payload)):
            raise UnsupportedPiece(
                "drawing method %d but %d bytes != %dx%d — unhandled layout "
                % (dos_method, len(payload), w, height))
        # Mac CTL requires the width to divide evenly by 8 (TLBFORM.TXT).  A
        # DOS-only width of 4 truncates to a ZERO-width piece -- which is what
        # silently blanked the converted wall sets.  Refuse rather than emit it.
        if to_mac and w % 8:
            raise UnsupportedPiece(
                "width %d is not a multiple of 8 — a Mac CTL cannot represent it "
                "(it would truncate to width %d)" % (w, w // 8 * 8))
        new_w4 = w // (8 if to_mac else 4)
        new_method = _swap_flag_byte(method, to_mac)
        body = (deplanarize(payload, w, height) if to_mac
                else planarize(payload, w, height))
    elif _is_colour_table(method):
        # The COLOUR TABLE (per-set palette).  Its header is u16 cycling /
        # u16 first colour / u16 colour count, then two BYTES (cycle-range count,
        # block-type byte) -- so the words swap and the RGB payload passes through
        # verbatim, but the BLOCK-TYPE BYTE IS STILL AN ENTRY FLAG BYTE and its
        # high nibble must be remapped like any other.
        #
        # This line used to read `new_method = method` -- "byte-exact against the
        # Yezukriis pair", and it was, because every palette in that pair is 0x08
        # (high nibble already 0, so the remap is a no-op).  POR's WALL libraries
        # use 0x18, which must become 0xc8.  That single unremapped nibble was the
        # only difference between our conversion and the real Mac art: 9 bytes in
        # 8X8DB.CTL, 7 in 8X8DC.CTL, and nothing else in 528K.
        #
        # A GROUND-TRUTH TEST ONLY COVERS WHAT THE GROUND TRUTH CONTAINS.
        new_w4, new_method, body = w4, _swap_flag_byte(method, to_mac), payload
    else:
        raise UnsupportedPiece(
            "entry is neither a known drawing method nor a colour table "
            "(trailing byte 0x%02x; expected a draw method or magic 8/24)" % method)

    return (struct.pack(dst + "Hhh", height, voff, hoff)
            + bytes([new_w4, new_method]) + body)


def _swap_u16_array(b):
    """Byte-swap a flat u16 array (the wall library's index entry)."""
    out = bytearray(b)
    for i in range(0, len(b) - 1, 2):
        out[i], out[i + 1] = b[i + 1], b[i]
    return bytes(out)


def convert(data, to=None):
    """Convert a whole container.  `to` = b'GLIB' | b'HLIB' (default: the other one).

    Containers NEST.  The wall libraries (8x8db/8x8dc) are a GLIB-of-GLIBs: their
    `tag` is the magic itself, and each entry is a complete sub-container (one
    wall SET, 48 pieces).  Treating those entries as flat tiles leaves the inner
    containers in the source byte order — the engine then reads garbage and the
    3D view goes BLACK, which is exactly what happened before this recursed.
    """
    c = parse(data)
    if to is None:
        to = GLIB if c["magic"] == HLIB else HLIB
    if to == c["magic"]:
        return data
    to_mac = (to == GLIB)
    e = ">" if to_mac else "<"

    if c["tag"] in (HLIB, GLIB):                     # container-of-containers
        entries = [convert(x, to) if x[:4] in (HLIB, GLIB) else _swap_u16_array(x)
                   for x in c["entries"]]
    else:
        entries = [_convert_entry(x, to_mac) for x in c["entries"]]

    # Rebuild the offset table.  Entry sizes are preserved by every transform we
    # support, so offsets carry over -- but recompute rather than assume.
    off, offsets = HDR + 4 * (c["count"] + 1), []
    for x in entries:
        offsets.append(off)
        off += len(x)
    offsets.append(off)

    # A nested library names its own magic in `tag` — retarget that too.
    tag = to if c["tag"] in (HLIB, GLIB) else c["tag"]

    out = bytearray()
    out += to
    out += struct.pack(e + "I", off)
    out += struct.pack(e + "H", c["count"])
    out += bytes([c["unused"], c["id_table"]])      # two BYTES — never swapped
    out += tag
    out += struct.pack(e + "%dI" % (c["count"] + 1), *offsets)
    for x in entries:
        out += x
    return bytes(out)


# --- filenames -------------------------------------------------------------
#
# Design art is per-id: the engine builds the MAC name (e.g. `8x8db3001.ctl`,
# visible in Hatari's "have to clip 1 chars from '8x8db3001.ctl'" warnings).
# DOS is limited to 8.3, so 4-letter prefixes (BIGP/CPIC/PICE/PICF/SPRI) already
# match and only the extension changes -- verified across the Yezukriis pair,
# whose nine basenames are identical in both releases.
#
# The wall sets are the exception: Mac `8x8db<id>` is 9 chars, so DOS truncates
# it to `8X8D<id>`.  That collides with `8x8dc` and we have NOT confirmed how DOS
# disambiguates the two sets, so we refuse to guess.

# --- mono (.tlb) synthesis ---------------------------------------------------
#
# The ST-mono (BWMODE) build probes per-id `.tlb` overrides, so a converted PC
# module used to play mono with BASE 1-bit art -- or worse, the engine's mono
# probe OPENED the design's original HLIB .TLB and read garbage into the pool.
# Synthesize a 1-bit GLIB from the converted colour art instead.
#
# GROUND TRUTH (base-game .CTL/.TLB pairs, measured 2026-07-17):
#   - the mono scale factor is PER FAMILY: walls 8x8 -> 16x16 (x2), BACK and
#     PIC[A-F] 88x88 -> 176x176 (x2), CPIC 24x24 -> 32x32 (x4/3), SPRIT x4/3,
#     BIGPIC 120x304 -> 180x456 (x3/2);
#   - mono item flags carry high nibble 0x9; low nibble 0 = plain 1bpp plane,
#     1 = keep-mask + data plane pair (jt995's composite: (dst & A) ^ B, A bit
#     set = dest kept, B bit set = white), 2 = PackBits-compressed 1bpp,
#     3 = the mode-3 stream (encoder unknown -- SPRIT approximated as mode 1),
#     8 = the palette item;
#   - every mono sub-GLIB's palette item is the same shape: header
#     00000000 0010 0098 + 16 RGB triplets (the standard 16-colour block).
#
# INK MODEL: the engine's g_dsp_ink LUT classes a pixel by luminance
# (2r+5g+b)>>3, ink below 112.  Straight thresholding loses all texture, so
# synthesis orders-dithers around that threshold (Bayer 4x4) -- the same
# visual language as the hand-dithered Mac art.  Best-effort by construction:
# the Mac's own 1-bit art was authored, not derived.

MONO_PAL_ITEM = bytes.fromhex("0000000000100098") + bytes.fromhex(
    "000000ffffff00ab0000ababab0000ab00abab5700ababab"
    "5757575757ff57ff5757ffffff5757ff57ffffff57ffffff")

_BAYER4 = (( 0,  8,  2, 10),
           (12,  4, 14,  6),
           ( 3, 11,  1,  9),
           (15,  7, 13,  5))

#             stem     num den  mode        ('pack' = 0x92, 'planar' = 0x90/91)
MONO_FAMILIES = {
    "8x8d": (2, 1, "planar"),
    "back": (2, 1, "pack"),
    "pic":  (2, 1, "pack"),          # pica..picf
    "cpic": (4, 3, "planar"),
    "spri": (4, 3, "planar"),        # base uses mode 3; approximated
    "bigp": (3, 2, "pack"),
    "dung": (4, 3, "planar"),        # DUNGCOM combat tiles: 24x24 -> 32x32
    "wild": (4, 3, "planar"),        # WILDCOM likewise (measured base pairs)
    "cbod": (4, 3, "planar"),        # CBODY combat bodies: 24x24 -> 32x32
    "coms": (4, 3, "planar"),        # COMSPR combat sprites: likewise
}


def mono_family(filename):
    """Per-family (scale_num, scale_den, mode) from the DOS art filename."""
    b = os.path.basename(filename).lower()
    for stem, fam in MONO_FAMILIES.items():
        if b.startswith(stem):
            return fam
    if b.startswith("pic"):
        return MONO_FAMILIES["pic"]
    return None


def _palette_rgb(entries):
    """256-entry (r,g,b) table from a colour container's palette item."""
    table = [(0, 0, 0)] * 256
    # sensible defaults for the UI range so out-of-band indices class sanely
    table[15] = (255, 255, 255)
    for e in entries:
        if len(e) >= 8 and _is_colour_table(e[7]):
            first, count = struct.unpack(">hh", e[2:6])
            pay = e[8:]
            for k in range(min(count, len(pay) // 3)):
                idx = first + k
                if 0 <= idx < 256:
                    table[idx] = (pay[k*3], pay[k*3+1], pay[k*3+2])
    return table


def _synth_item(ent, pal, num, den, mode):
    """One colour GLIB image item -> one mono GLIB item."""
    rows, yb, xb = struct.unpack(">Hhh", ent[0:6])
    w4, method = ent[6], ent[7]
    w = w4 * 8
    payload = ent[8:]
    if w == 0 or rows == 0:
        # degenerate/stub slot (the wall masters carry a few zero-width
        # oddities besides the plain <8-byte stubs) — pass through; the
        # engine's own stub handling covers them
        return bytes(ent)
    lo = method & 0x0F
    m23mask = None
    if lo == 9:                                    # frame-sequence table
        return bytes(ent)                          # not pixels; keep verbatim
    if (w4, method) == (0x00, 0x80):               # type-128 composite table
        return bytes(ent)                          # base mono twins keep it
    if lo == 2:                                    # PackBits 8bpp
        px = rle_decode(payload, w * rows)
    elif lo == 7:                                  # method 23 (linear, in .ctl)
        px, m23mask, used = m23_decode(payload, w, rows, planar=False)
        if used != len(payload):
            raise UnsupportedPiece("mono synth: method 23 stream mismatch")
    elif w * rows == len(payload):                 # plain linear 8bpp
        px = payload
    elif 2 * w * rows == len(payload):             # AND/OR mask pair
        px = payload[w * rows:]
        # transparency comes from the AND plane (0xFF = dest kept)
        m23mask = bytes(0 if b == 0xFF else 1 for b in payload[:w * rows])
    else:
        raise UnsupportedPiece(
            "mono synth: unrecognized colour payload (%d bytes for %dx%d)"
            % (len(payload), w, rows))

    ow = (w * num + den - 1) // den
    oh = (rows * num + den - 1) // den
    owb = (ow + 7) // 8

    lum = [0] * 256
    for i in range(256):
        r, g, b = pal[i]
        lum[i] = (2 * r + 5 * g + b) >> 3

    data = bytearray(owb * oh)
    mask = bytearray(owb * oh)
    has_mask = False
    for y in range(oh):
        sy = min(y * den // num, rows - 1)
        for x in range(ow):
            sx = min(x * den // num, w - 1)
            p = px[sy * w + sx]
            bit = 0x80 >> (x & 7)
            transparent = (p == 255 if m23mask is None
                           else not m23mask[sy * w + sx])
            if transparent and mode == "planar":   # transparent key
                mask[y * owb + (x >> 3)] |= bit
                has_mask = True
                continue
            # ordered dither around the g_dsp_ink threshold (112)
            if lum[p] >= 64 + _BAYER4[y & 3][x & 3] * 6:
                data[y * owb + (x >> 3)] |= bit

    oyb = yb * num // den
    oxb = xb * num // den
    if mode == "pack":
        # ROW-ALIGNED PackBits: the mono decoder unpacks per row (measured:
        # base BACK.TLB streams have ZERO packets crossing a row boundary),
        # and a whole-bitmap stream desynchronizes it — POR's Back1004.tlb
        # was the first live 0x92 synth item and it ADDRESS-ERRORED the
        # ST-mono walk. Encode each row as its own packet sequence.
        body = b"".join(rle_encode(bytes(data[r*owb:(r+1)*owb]))
                        for r in range(oh))
        flags = 0x92
    elif has_mask:
        body = bytes(mask) + bytes(data)           # plane A = keep, B = data
        flags = 0x91
    else:
        body = bytes(data)
        flags = 0x90
    return struct.pack(">Hhh", oh, oyb, oxb) + bytes([owb, flags]) + body


def mono_synth(glib, family):
    """A colour GLIB container (post-convert) -> a synthesized 1-bit GLIB.

    `family` is a (scale_num, scale_den, mode) tuple from `mono_family()`.
    Recurses into nested sub-GLIBs; stubs and degenerate slots pass through
    (the engine's mirror-synthesis handles wall stubs itself).
    """
    num, den, mode = family
    p = parse(glib)
    if p["magic"] != GLIB:
        raise BadContainer("mono_synth expects a Mac GLIB (convert first)")
    pal = _palette_rgb(p["entries"])
    out = []
    for e in p["entries"]:
        if e[:4] == GLIB:
            out.append(mono_synth(e, family))
        elif len(e) >= 8 and _is_colour_table(e[7]):
            out.append(MONO_PAL_ITEM)
        elif len(e) < 8:
            out.append(bytes(e))                   # stub slot
        else:
            out.append(_synth_item(e, pal, num, den, mode))
    off = HDR + 4 * (len(out) + 1)
    offsets = [off]
    for e in out:
        off += len(e)
        offsets.append(off)
    hdr = (GLIB + struct.pack(">I", off) + struct.pack(">H", len(out))
           + bytes([p["unused"], p["id_table"]]) + p["tag"])
    return hdr + struct.pack(">%dI" % len(offsets), *offsets) + b"".join(out)


def mac_name(dos_name, dos83=False):
    """DOS art filename -> the name the ENGINE derives and probes first.

    2026-07-17 STEM AUDIT (every per-id probe site in src/engine/boot.c):

        family       engine probe                 DOS 8.3 file     rule
        walls        8x8d{b,c}<g><nnn>  (L6eea)   8X8D<g><nnn>     letter from
                                                                   id band:
                                                                   (id<10)?b:c
        big pics     bigpi{c,x}<d><nnn> (L579e)   BIGP<d><nnn>     letter from
                                                                   id band:
                                                                   (id<248)?c:x
        sprites      SPRIT<d><nnn>      (L541a)   SPRI<d><nnn>     expand
        backdrops    back<g><nnn>       (l33ac)   Back<g><nnn>     identity
        pictures     PIC[A-F]1<nnn>     (L541a)   PIC[A-F]1<nnn>   identity
        portraits    CPIC1<nnn>         (jt56)    CPIC1<nnn>       identity

    Every 8.3 spelling is the same uniform clip -- base[:4] + digit + id:03 --
    which is exactly what the engine retries as a fallback (ADR-0013).  With
    `dos83=True` the DOS stem is kept verbatim (extension swap only): the
    output then fits a real GEMDOS/FAT volume and the engine finds it through
    the ADR-0013 probe.  The default (expanded) spelling matches the primary
    probe and real Mac FRUA.
    """
    base, ext = os.path.splitext(os.path.basename(dos_name))
    if ext.lower() != ".tlb":
        raise ValueError("expected a .TLB DOS art file: %r" % dos_name)
    up = base.upper()
    if dos83:
        return base + ".ctl"
    if up.startswith("8X8D") and len(base) > 4 and base[4].isdigit():
        if len(base) != 8 or not base[4:].isdigit():
            raise ValueError("wall-set name %r is not 8X8D<g><nnn>" % base)
        group, setid = base[4], int(base[5:8])
        letter = "b" if setid < 10 else "c"
        return "8x8d%s%s%03d.ctl" % (letter, group, setid)
    if up.startswith("BIGP") and len(base) == 8 and base[4:].isdigit():
        digit, picid = base[4], int(base[5:8])
        letter = "c" if picid < 248 else "x"      # L579e id band
        return "bigpi%s%s%03d.ctl" % (letter, digit, picid)
    if up.startswith("SPRI") and len(base) == 8 and base[4:].isdigit():
        return "sprit%s.ctl" % base[4:8]
    return base.lower() + ".ctl"


def dos_name(mac_name_):
    base, ext = os.path.splitext(os.path.basename(mac_name_))
    if ext.lower() != ".ctl":
        raise ValueError("expected a .CTL Mac art file: %r" % mac_name_)
    low = base.lower()
    if (low.startswith("8x8db") or low.startswith("8x8dc")) \
            and len(base) == 9 and base[5:].isdigit():
        # inverse of the L6eea band rule: drop the letter (8.3 collapse),
        # but refuse a letter that contradicts the id band -- such a file
        # could never have come from a DOS module.
        setid = int(base[6:9])
        want = "b" if setid < 10 else "c"
        if low[4] != want:
            raise ValueError(
                "wall-set name %r contradicts the L6eea id-band rule "
                "(id %d wants '%s')" % (base, setid, want))
        return ("8X8D" + base[5:]).upper() + ".TLB"
    if (low.startswith("bigpic") or low.startswith("bigpix")) \
            and len(base) == 10 and base[6:].isdigit():
        picid = int(base[7:10])
        want = "c" if picid < 248 else "x"        # L579e id band
        if low[5] != want:
            raise ValueError(
                "big-picture name %r contradicts the L579e id-band rule "
                "(id %d wants '%s')" % (base, picid, want))
        return ("BIGP" + base[6:]).upper() + ".TLB"
    if low.startswith("sprit") and len(base) == 9 and base[5:].isdigit():
        return ("SPRI" + base[5:]).upper() + ".TLB"
    return base.upper() + ".TLB"


def main(argv):
    # 8.3 output is the DEFAULT (ADR-0013). The expanded Mac-convention
    # names are not just too long for a real GEMDOS/FAT volume -- on the
    # Hatari GEMDOS mount they are a COLLISION HAZARD: the probe and the
    # host names are both clipped to 8 chars before matching, so
    # 8x8db1001/1003/1005/1008/1009.ctl all become "8x8db100.ctl" and the
    # engine silently opens the FIRST one -- the wrong wall set, no error.
    # Measured live (BEOWOLF, 2026-07-17). --mac-names emits the expanded
    # spelling for use with real Mac FRUA (an HFS volume keeps long names).
    dos83 = True
    mono = True
    while argv and argv[0].startswith("--"):
        if argv[0] == "--mac-names":
            dos83 = False
        elif argv[0] == "--dos83":            # accepted, now the default
            pass
        elif argv[0] == "--no-mono":
            mono = False
        else:
            print("unknown flag %s" % argv[0], file=sys.stderr)
            return 2
        argv = argv[1:]
    if not argv:
        print(__doc__)
        return 2
    rc = 0
    for path in argv:
        data = open(path, "rb").read()
        try:
            out = convert(data)
        except (UnsupportedPiece, BadContainer) as exc:
            print("SKIP %s: %s" % (path, exc), file=sys.stderr)
            rc = 1
            continue
        except Exception as exc:            # never abort the whole batch
            print("SKIP %s: UNEXPECTED %s: %s"
                  % (path, type(exc).__name__, exc), file=sys.stderr)
            rc = 1
            continue
        try:
            dest = (mac_name(path, dos83) if data[:4] == HLIB
                    else dos_name(path))
        except ValueError as exc:
            print("SKIP %s: %s" % (path, exc), file=sys.stderr)
            rc = 1
            continue
        dest = os.path.join(os.path.dirname(path), dest)
        with open(dest, "wb") as fh:
            fh.write(out)
        print("%s -> %s (%d bytes)" % (path, dest, len(out)))
        # Mono synthesis: OVERWRITE the original DOS .TLB with a 1-bit GLIB
        # under the same name (the mono engine probes "<name>.tlb", and the
        # HLIB original there would otherwise be read into the pool as
        # garbage). The HLIB source is gone from this directory afterwards —
        # convert a staged/working copy, keep the module archive pristine.
        if mono and data[:4] == HLIB:
            fam = mono_family(path)
            if fam is None:
                print("NOTE %s: no mono family for this stem — .TLB kept as "
                      "HLIB (mono play will mis-read it)" % path,
                      file=sys.stderr)
                continue
            try:
                mout = mono_synth(out, fam)
            except (UnsupportedPiece, BadContainer) as exc:
                print("SKIP mono %s: %s" % (path, exc), file=sys.stderr)
                rc = 1
                continue
            with open(path, "wb") as fh:
                fh.write(mout)
            print("%s -> mono 1-bit GLIB in place (%d bytes)"
                  % (path, len(mout)))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
