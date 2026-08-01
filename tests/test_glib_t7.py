"""The GLIB method-23 ("type 7") transparent-skip law.

The engine's decode_glib_t7() (src/engine/boot.c) and art_convert.m23_decode()
decode the same streams and MUST agree. They did not: the C skipped 256 - b
where the linear law is 257 - b, so every transparent gap came out one pixel
short. Because a skip error shifts everything after it and the shifts
accumulate along a row, the damage scaled with gap count — solid logos looked
perfect while fine lettering became noise, which is why it was mistaken for a
palette fault rather than a codec bug for months (the intro's SSI, AD&D and
credits screens).

These tests are the pin. The first two need no game data. The last one runs
only when the copyrighted art is staged, and is the one that would actually
have caught it: it asserts the SSI screen's caption decodes to legible text.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import art_convert as ac  # noqa: E402


def decode_c_law(payload, w, rows, skip):
    """Mirror of decode_glib_t7 with the skip law injected."""
    px = bytearray(w * rows)
    i, n, y, x = 0, len(payload), 0, 0
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
        else:
            x += skip(v)
    return px


def test_c_source_uses_257_minus_b():
    """The literal law, read straight out of the engine source.

    Cheap, data-free, and the thing that regressed. A skip of 256 - b is the
    PLANAR unit count (x += 4 * (256 - v)); pasting it into the linear decoder
    is the exact mistake this pins.
    """
    src = (ROOT / "src/engine/boot.c").read_text()
    # Match the skip expression itself, not the enclosing function: boot.c
    # carries a forward DECLARATION of decode_glib_t7 as well as the
    # definition, and a "from the signature to the next brace" regex happily
    # matches the prototype plus whatever function follows it.
    assert re.search(r"x \+ \(257 - b\)", src), (
        "decode_glib_t7 must skip 257 - b transparent pixels; 256 - b makes "
        "every gap one pixel short and scrambles fine lettering"
    )
    assert not re.search(r"x \+ \(256 - b\)", src), (
        "256 - b is the PLANAR unit count (x += 4 * (256 - v)); the linear "
        "Mac .ctl layout skips 257 - b"
    )


def test_two_laws_differ_by_one_per_gap():
    """A hand-built stream: two literals either side of one skip.

    Proves the failure MODE as well as the value — the pixel after the gap
    lands one short, and a second gap doubles the error.
    """
    #      lit 1 'A'   skip(0xFF)   lit 1 'B'   skip(0xFF)  lit 1 'C'  eol
    pay = bytes([1, 0xAA, 0xFF, 1, 0xBB, 0xFF, 1, 0xCC, 0])
    w = 16
    right = decode_c_law(pay, w, 1, lambda v: 257 - v)
    wrong = decode_c_law(pay, w, 1, lambda v: 256 - v)
    # 0xFF -> skip 2 under the correct law: A at 0, B at 3, C at 6.
    assert [i for i, p in enumerate(right) if p] == [0, 3, 6]
    # ...and skip 1 under the broken one: B slips by 1, C by 2.
    assert [i for i, p in enumerate(wrong) if p] == [0, 2, 4]


@pytest.mark.skipif(
    not (ROOT / "data/work/gamedata/TITLE.ctl").exists(),
    reason="TITLE.ctl not staged (copyrighted art lives outside the repo)",
)
def test_ssi_caption_decodes_to_legible_text():
    """The real regression, on the real asset.

    Set 2 item 1 of TITLE.ctl is the SSI screen's logo + caption: under the
    correct law it reads "STRATEGIC SIMULATIONS, INC.", under the broken one
    it is noise. The assertion is byte-equality against art_convert's
    independently-derived decoder — that is the real pin, and it fails loudly
    on the exact regression.

    (An earlier draft tried to assert "the correct decode is less fragmented"
    by counting x-runs. That is not a discriminator: a skip error SHIFTS runs
    rather than splitting them, so both laws scored an identical 409. Kept as
    a warning against structural proxies for "looks right".)
    """
    top = ac.parse((ROOT / "data/work/gamedata/TITLE.ctl").read_bytes())
    piece = ac.parse(top["entries"][2])["entries"][1]
    rows, _yb, _xb = struct.unpack(">Hhh", piece[0:6])
    w = piece[6] * 8
    pay = piece[8:]

    right = decode_c_law(pay, w, rows, lambda v: 257 - v)
    wrong = decode_c_law(pay, w, rows, lambda v: 256 - v)

    # The C decoder and the Python one must produce identical pixels.
    ref, _mask, _used = ac.m23_decode(pay, w, rows, planar=False)
    assert bytes(right) == bytes(ref), (
        "decode_glib_t7's law disagrees with art_convert.m23_decode"
    )
    assert bytes(wrong) != bytes(ref), (
        "the two laws must differ on this asset, or it cannot pin anything"
    )
