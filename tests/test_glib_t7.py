"""The GLIB method-23 ("type 7") transparent-skip law, linear Mac layout.

In a Mac .ctl a skip byte b (0x80..0xFF) means 256 - b transparent pixels.
The engine's decode_glib_t7() (src/engine/boot.c), art_convert.m23_decode()
and the C converter (src/convert/artconv.c) must all use it.

From 2026-07-31 to 2026-11-22 all of them used 257 - b. The July change was
"proven" on art our own converter had written, whose encoder ALSO used
257 - b, so the errors cancelled and only SSI's real Mac files came out
scrambled: a skip error shifts everything after it along the row, so solid
logos looked fine while fine lettering (the SSI, AD&D and credits text of the
intro) turned to noise. Never check this law on art we wrote ourselves.

The data-free tests pin the source and the encoder. The staged-data tests
check the law against SSI's OWN files: every colour type-7 piece SSI shipped
fits its declared width under 256 - b (none under 257 - b fit), and the Mac
TITLE.CTL decoded this way lines up with the DOS TITLE.TLB, whose planar law
is independently proven.
"""
import re
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import art_convert as ac  # noqa: E402

MAC_DIR = ROOT / "data/work/gamedata-mac"
MAC_TITLE = MAC_DIR / "TITLE.CTL"
DOS_TITLE = ROOT / "data/dos-frua/DISK1/TITLE.TLB"


def decode_c_law(payload, w, rows, skip):
    """Mirror of decode_glib_t7 with the skip law injected. Returns
    (pixels, the widest x any row reached)."""
    px = bytearray(w * rows)
    i, n, y, x, reach = 0, len(payload), 0, 0, 0
    while y < rows and i < n:
        v = payload[i]
        i += 1
        if v == 0:
            y += 1
            x = 0
        elif v < 128:
            for _ in range(v):
                if i >= n:
                    break
                if 0 <= x < w:
                    px[y * w + x] = payload[i]
                i += 1
                x += 1
            reach = max(reach, x)
        else:
            x += skip(v)
    return px, reach


def law256(v):
    return 256 - v


def law257(v):
    return 257 - v


def test_engine_source_uses_256_minus_b():
    src = (ROOT / "src/engine/boot.c").read_text()
    assert re.search(r"x \+ \(256 - b\)", src), (
        "decode_glib_t7 must skip 256 - b transparent pixels")
    assert not re.search(r"x \+ \(257 - b\)", src), (
        "257 - b is the July 2026 misfix: it scrambles SSI's own Mac art")


def test_converter_sources_use_256_minus_v():
    c = (ROOT / "src/convert/artconv.c").read_text()
    py = (ROOT / "tools/art_convert.py").read_text()
    for name, src in (("artconv.c", c), ("art_convert.py", py)):
        start = src.index("m23_decode(")
        body = src[start:src.index("m23_encode(", start)]
        code = [ln for ln in body.splitlines()
                if re.search(r"\bx\s*\+?=[^\[]*257", ln)]
        assert not code, name + ": m23_decode still skips 257: %r" % code


def test_a_skip_byte_is_256_minus_b():
    #      lit 'A'   skip 0xFF   lit 'B'   skip 0xFE   lit 'C'  eol
    pay = bytes([1, 0xAA, 0xFF, 1, 0xBB, 0xFE, 1, 0xCC, 0])
    px, _ = decode_c_law(pay, 16, 1, law256)
    assert [i for i, p in enumerate(px) if p] == [0, 2, 5]
    ref, mask, _ = ac.m23_decode(pay, 16, 1, planar=False)
    assert bytes(ref) == bytes(px)


def test_linear_encoder_round_trips_one_pixel_gaps():
    """256 - b can say "skip 1", so the encoder no longer has to paint
    1-pixel gaps opaque (the 257 - b encoder did)."""
    w, rows = 300, 3
    px = bytearray(w * rows)
    mask = bytearray(w * rows)
    pattern = [(0, 5), (1, 9), (3, 7), (4, 1), (140, 2), (299, 3)]
    for y in range(rows):
        for x, v in pattern:
            x2 = min(w - 1, x + y)
            px[y * w + x2] = v
            mask[y * w + x2] = 1
    stream = ac.m23_encode(px, mask, w, rows, planar=False)
    assert 0xFF in stream                     # a 1-pixel skip was emitted
    dpx, dmask, used = ac.m23_decode(stream, w, rows, planar=False)
    assert used == len(stream)
    assert bytes(dmask) == bytes(mask)
    assert bytes(dpx) == bytes(px)


def test_converted_glib_carries_the_trailer():
    """A GLIB written by the converter ends in ARTCONV_TRAILER, past its
    declared size, so the engine can regenerate older conversions. Header
    byte [10] stays SSI's: the engine rejects a GLIB with it set."""
    if not DOS_TITLE.exists():
        pytest.skip("DOS TITLE.TLB not staged")
    out = ac.convert(DOS_TITLE.read_bytes())
    size = struct.unpack(">I", out[4:8])[0]
    assert out[:4] == b"GLIB" and out[10] == 0
    assert out[size:] == ac.ARTCONV_TRAILER
    # the trailer is no obstacle to converting back (re-encoded, so the
    # bytes differ from SSI's DOS file; the HLIB carries no trailer)
    back = ac.convert(out)
    assert back[:4] == b"HLIB"
    assert struct.unpack("<I", back[4:8])[0] == len(back)


def _type7_pieces(container):
    for e in container["entries"]:
        if e[:4] == b"GLIB":
            yield from _type7_pieces(ac.parse(e))
        elif len(e) >= 8 and (e[7] & 15) == 7 and e[7] & 0x40:
            yield e


@pytest.mark.skipif(not MAC_DIR.is_dir(),
                    reason="the Mac release's art is not staged")
def test_ssi_mac_art_fits_its_width_only_under_256():
    """Every colour type-7 piece SSI shipped on the Mac stays inside its
    declared width under 256 - b; under 257 - b rows run off the piece."""
    n = over256 = over257 = 0
    for f in sorted(MAC_DIR.iterdir()):
        if f.suffix.upper() != ".CTL":
            continue
        data = f.read_bytes()
        if data[:4] != b"GLIB":
            continue
        assert not data.endswith(ac.ARTCONV_TRAILER), f.name + " is ours"
        for e in _type7_pieces(ac.parse(data)):
            h = struct.unpack(">H", e[:2])[0]
            w = e[6] * 8
            n += 1
            over256 += decode_c_law(e[8:], w, h, law256)[1] > w
            over257 += decode_c_law(e[8:], w, h, law257)[1] > w
    assert n >= 40
    assert over256 == 0
    assert over257 > n // 4                   # the law is discriminated


def _mask_linear(e, law):
    """The set of plotted (x, y), shifted one column right: the DOS planar
    layout starts sweep p at x = p + 1, so its art sits one pixel right of
    the Mac's."""
    h = struct.unpack(">H", e[:2])[0]
    pay, i, m = e[8:], 0, set()
    for y in range(h):
        x = 0
        while i < len(pay):
            v = pay[i]
            i += 1
            if v == 0:
                break
            if v >= 128:
                x += law(v)
                continue
            for _ in range(v):
                m.add((x + 1, y))
                i += 1
                x += 1
    return h, m


def _mask_dos(e):
    h = struct.unpack("<H", e[:2])[0]
    w = e[6] * 4
    _px, mask, _ = ac.m23_decode(e[8:], w, h, planar=True)
    return h, {(i % w, i // w) for i, m in enumerate(mask) if m}


@pytest.mark.skipif(not (MAC_TITLE.exists() and DOS_TITLE.exists()),
                    reason="the Mac and DOS TITLE art are not both staged")
def test_mac_title_matches_dos_title_under_256():
    """SSI's Mac TITLE.CTL against the DOS TITLE.TLB, piece by piece (the
    SSI, AD&D, logo and credits screens). Under 256 - b the transparency
    masks agree exactly on most pieces and to within 1% on the rest
    (measured 2026-11-22: 11, 0, 0, 0, 0, 81, 0 pixels); under 257 - b they
    are thousands of pixels apart."""
    mac = ac.parse(MAC_TITLE.read_bytes())
    dos = ac.parse(DOS_TITLE.read_bytes())
    exact = 0
    for s, it in ((2, 1), (2, 2), (3, 1), (3, 2), (4, 2), (6, 1), (6, 2)):
        me = ac.parse(mac["entries"][s])["entries"][it]
        de = ac.parse(dos["entries"][s])["entries"][it]
        hd, d = _mask_dos(de)
        h, m256 = _mask_linear(me, law256)
        _, m257 = _mask_linear(me, law257)
        assert h == hd, (s, it)
        assert len(m256 ^ d) * 100 <= len(d), (s, it, len(m256 ^ d))
        assert len(m256 ^ d) < len(m257 ^ d), (s, it)
        exact += not (m256 ^ d)
    assert exact >= 4
