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

NOT SUPPORTED YET: piece type 2 (low nibble of `flags`) is RLE-compressed and
the two releases use *different* codecs -- the Mac side is PackBits (jt1171 =
_UnpackBits), the DOS side is its own scheme and is not yet decoded.  In the
Yezukriis pair this is exactly the two BIGP (big picture) payloads.  Such
entries are REFUSED, loudly, rather than silently mangled -- see
`UnsupportedPiece`.
"""
import os
import struct
import sys

HLIB, GLIB = b"HLIB", b"GLIB"
HDR = 16                      # magic + size + count + flags + tag
TYPE_COMPRESSED = 0x2         # low nibble of the entry flag byte


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
    magic = data[:4]
    e = _endian(magic)
    size, = struct.unpack(e + "I", data[4:8])
    count, flags = struct.unpack(e + "HH", data[8:12])
    tag = data[12:16]
    end = HDR + 4 * (count + 1)
    if end > len(data):
        raise BadContainer("offset table runs past EOF")
    offsets = list(struct.unpack(e + "%dI" % (count + 1), data[HDR:end]))
    entries = [data[offsets[i]:offsets[i + 1]] for i in range(count)]
    return dict(magic=magic, endian=e, size=size, count=count, flags=flags,
                tag=tag, offsets=offsets, entries=entries)


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


def _convert_entry(ent, to_mac):
    """Convert one entry (8-byte header + payload)."""
    if len(ent) < 8:
        return ent                                   # empty / degenerate slot
    src = "<" if to_mac else ">"       # CURRENT order: HLIB when heading to Mac
    dst = ">" if to_mac else "<"
    rows, xhot, yhot = struct.unpack(src + "Hhh", ent[0:6])
    stride, flags = ent[6], ent[7]
    payload = ent[8:]

    if (flags & 0x0F) == TYPE_COMPRESSED:
        raise UnsupportedPiece(
            "piece type 2 (RLE) — DOS and Mac use different codecs; "
            "the DOS side is not decoded yet")

    new_flags = _swap_flag_byte(flags, to_mac)

    # Width in bytes: HLIB keeps W/4 (its per-plane stride), GLIB keeps W/8.
    w = stride * (4 if to_mac else 8)     # decode with the SOURCE's unit
    if w and rows and w * rows == len(payload):
        new_stride = w // (8 if to_mac else 4)
        body = (deplanarize(payload, w, rows) if to_mac
                else planarize(payload, w, rows))
    else:
        # Geometry does not describe the payload (palette / opaque blob):
        # swap the header words, copy the bytes through untouched.
        new_stride, new_flags = stride, flags
        body = payload

    return struct.pack(dst + "Hhh", rows, xhot, yhot) + bytes([new_stride, new_flags]) + body


def convert(data, to=None):
    """Convert a whole container.  `to` = b'GLIB' | b'HLIB' (default: the other one)."""
    c = parse(data)
    if to is None:
        to = GLIB if c["magic"] == HLIB else HLIB
    if to == c["magic"]:
        return data
    to_mac = (to == GLIB)
    e = ">" if to_mac else "<"

    entries = [_convert_entry(x, to_mac) for x in c["entries"]]

    # Rebuild the offset table.  Entry sizes are preserved by every transform we
    # support, so offsets carry over -- but recompute rather than assume.
    off, offsets = HDR + 4 * (c["count"] + 1), []
    for x in entries:
        offsets.append(off)
        off += len(x)
    offsets.append(off)

    out = bytearray()
    out += to
    out += struct.pack(e + "I", off)
    out += struct.pack(e + "HH", c["count"], c["flags"])
    out += c["tag"]
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
    """DOS art filename -> Mac.  Raises on the ambiguous wall-set prefix."""
    base, ext = os.path.splitext(os.path.basename(dos_name))
    if ext.lower() != ".tlb":
        raise ValueError("expected a .TLB DOS art file: %r" % dos_name)
    if base.upper().startswith("8X8D") and len(base) > 4 and base[4].isdigit():
        raise ValueError(
            "wall-set name %r is ambiguous: DOS 8.3 truncates both 8x8db<id> "
            "and 8x8dc<id> to 8X8D<id>; the Mac target cannot be inferred" % base)
    return base.lower() + ".ctl"


def dos_name(mac_name_):
    base, ext = os.path.splitext(os.path.basename(mac_name_))
    if ext.lower() != ".ctl":
        raise ValueError("expected a .CTL Mac art file: %r" % mac_name_)
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
