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

NOT SUPPORTED YET
-----------------
1. **piece type 2** (low nibble of `flags`) is RLE-compressed and the two
   releases use *different* codecs -- the Mac side is PackBits (jt1171 =
   _UnpackBits), the DOS side is its own scheme and is not decoded.  In the
   Yezukriis pair this is exactly the two BIGP (big picture) payloads.  These are
   REFUSED loudly -- see `UnsupportedPiece`.

2. **THE WALL LIBRARIES (8x8db / 8x8dc) DO NOT CONVERT CORRECTLY.**  They are a
   container-of-containers (10 wall SETS x 48 pieces) and `convert()` does
   recurse into them -- but installing a converted wall library over the base
   game's renders a **BLACK 3D view** in the engine.  Verified with Game39 (Pool
   of Radiance).  423 of its 432 wall pieces parse as plain tiles and 9 fall into
   the `_convert_entry` "opaque" fallback below, which copies a payload it does
   not understand straight through -- so at minimum those 9 are garbage, and the
   black view says something further is wrong (the per-set palette/colour-range
   is the prime suspect; see docs/glib-palette-subsystem.md).
   **Do not ship converted wall art until this is root-caused.**  Pictures and
   sprites are fine -- those are proven byte-exact against real Mac art.

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
    """Method-23 stream -> (pixels, mask).  mask[i] = 1 where a pixel was plotted."""
    px, mask = bytearray(w * rows), bytearray(w * rows)
    i, n = 0, len(payload)
    step = 4 if planar else 1
    for sweep in range(4 if planar else 1):
        for y in range(rows):
            x = sweep
            while True:
                if i >= n:
                    return px, mask, i          # truncated: caller decides
                v = payload[i]; i += 1
                if v == 0:
                    break                        # end of row
                if v >= 128:
                    x += (257 - v) * step        # transparent skip
                    continue
                for _ in range(v):              # literal run
                    if i >= n:
                        return px, mask, i
                    if 0 <= x < w:
                        px[y * w + x] = payload[i]
                        mask[y * w + x] = 1
                    i += 1
                    x += step
    return px, mask, i


def m23_encode(px, mask, w, rows, planar):
    """(pixels, mask) -> a method-23 stream in the requested layout."""
    out = bytearray()
    step = 4 if planar else 1
    for sweep in range(4 if planar else 1):
        for y in range(rows):
            cols = list(range(sweep, w, step)) if planar else list(range(w))
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
        # Mac -> DOS is not blocked: the CTL layout is SOLVED (m23_decode/encode
        # with planar=False round-trips SSI's own streams byte-for-byte).
        if not to_mac:
            if not (w and height):
                raise UnsupportedPiece("method 23 entry with no geometry")
            px, mask, used = m23_decode(payload, w, height, planar=False)
            if used != len(payload):
                raise UnsupportedPiece("method 23 stream consumed %d of %d bytes"
                                       % (used, len(payload)))
            # ...but we cannot WRITE the DOS layout until it is solved either.
            raise UnsupportedPiece(
                "drawing method 23 (compressed transparent): the DOS sweep "
                "layout is not solved -- see the note in this file")
        raise UnsupportedPiece(
            "drawing method 23 (compressed transparent): the DOS sweep layout is "
            "NOT SOLVED. Refusing rather than emitting garbage.\n"
            "  KNOWN: the codec is skip/literal, per row -- 0 = end of row, "
            "v>=128 = skip (257-v) transparent pixels, v<128 = v literal bytes.\n"
            "  KNOWN: the MAC/CTL layout is SOLVED and CONFIRMED -- one linear "
            "pass, each row plotted completely. It consumes SSI's own streams "
            "exactly (615/615 bytes on POR's PICA1003) and yields a clean sprite.\n"
            "  UNSOLVED: how the DOS/TLB stream's 4 sweeps map to columns. The "
            "4-sweep structure is right (the DOS payload is longer by almost "
            "exactly the extra row terminators) and the ROW SET and the OPAQUE "
            "PIXEL COUNT both come out exact -- only the COLUMNS land wrong. "
            "Tried and rejected: stride-4 interleave (x = p + 4c), contiguous "
            "quarters (x = 22p + c), both sweep orders, both skip scalings.\n"
            "  GROUND TRUTH IS AVAILABLE: the Mac edition of Pool of Radiance "
            "ships these same pictures natively (data/work/fanmods/pormac). "
            "Solve it against that, not by re-reading DRAW23.TXT.")

    if dos_method == DRAW_ID_LIST:
        raise UnsupportedPiece("drawing method 25 (image-ID list)")

    if dos_method in DRAW_UNCOMPRESSED:
        if not (w and height and w * height == len(payload)):
            raise UnsupportedPiece(
                "drawing method %d but %d bytes != %dx%d — unhandled layout "
                "(method 17 stores an AND+OR mask pair)" % (dos_method, len(payload), w, height))
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

def mac_name(dos_name):
    """DOS art filename -> Mac.

    The wall-set prefix looked ambiguous (DOS 8.3 collapses both 8x8db<id>
    and 8x8dc<id> to 8X8D<id>) -- but the ENGINE resolves it: the faithful
    wall loader (CODE 7 L6eea, `l6eea` in src/engine/boot.c) derives the
    letter from the SET-ID BAND, `(id < 10) ? 'b' : 'c'`, before probing the
    per-id override "<base><group><id:03>".  So `8X8D<g><nnn>.TLB` maps
    deterministically: nnn 001..009 -> 8x8db<g><nnn>, nnn >= 010 ->
    8x8dc<g><nnn>.  (8X8DB has sets 1..9(10); 8X8DC carries 10+ -- matches
    the base libraries' own split.)
    """
    base, ext = os.path.splitext(os.path.basename(dos_name))
    if ext.lower() != ".tlb":
        raise ValueError("expected a .TLB DOS art file: %r" % dos_name)
    if base.upper().startswith("8X8D") and len(base) > 4 and base[4].isdigit():
        if len(base) != 8 or not base[4:].isdigit():
            raise ValueError("wall-set name %r is not 8X8D<g><nnn>" % base)
        group, setid = base[4], int(base[5:8])
        letter = "b" if setid < 10 else "c"
        return "8x8d%s%s%03d.ctl" % (letter, group, setid)
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
    return base.upper() + ".TLB"


def main(argv):
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
        try:
            dest = (mac_name(path) if data[:4] == HLIB else dos_name(path))
        except ValueError as exc:
            print("SKIP %s: %s" % (path, exc), file=sys.stderr)
            rc = 1
            continue
        dest = os.path.join(os.path.dirname(path), dest)
        with open(dest, "wb") as fh:
            fh.write(out)
        print("%s -> %s (%d bytes)" % (path, dest, len(out)))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
