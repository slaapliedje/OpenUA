"""Tests for tools/art_convert.py — the DOS HLIB <-> Mac GLIB art converter.

The synthetic tests below pin the transform (container endianness, the entry
header's stride/flag re-encoding, and the Mode-X <-> linear pixel shuffle).

The last test is the one that actually *proves* it: it converts the real DOS
art of "The Curse of Yezukriis" and compares byte-for-byte against the real Mac
art of the same module.  That pair is the ground truth the converter was derived
from.  It is copyrighted fan content under the git-ignored data/, so the test
SKIPS when the module is not staged (see docs/fan-module-hacks.md).
"""
import glob
import os
import struct

import pytest

from art_convert import (BadContainer, UnsupportedPiece, convert, deplanarize,
                         dos_name, mac_name, parse, planarize, rle_decode,
                         rle_encode)


def _container(magic, entries, tag=b"TILE", flags=0):
    """Build an HLIB/GLIB container around raw entry blobs."""
    e = ">" if magic == b"GLIB" else "<"
    n = len(entries)
    off = 16 + 4 * (n + 1)
    offsets = []
    for x in entries:
        offsets.append(off)
        off += len(x)
    offsets.append(off)
    out = bytearray(magic)
    out += struct.pack(e + "I", off)
    out += struct.pack(e + "HH", n, flags)
    out += tag
    out += struct.pack(e + "%dI" % (n + 1), *offsets)
    for x in entries:
        out += x
    return bytes(out)


def _tile(magic, rows, w, pixels, flag_lo=5):
    """One entry: 8-byte header + payload, in `magic`'s conventions."""
    e = ">" if magic == b"GLIB" else "<"
    stride = w // (8 if magic == b"GLIB" else 4)
    flags = (0xC0 if magic == b"GLIB" else 0x10) | flag_lo
    return (struct.pack(e + "Hhh", rows, 0, 0)
            + bytes([stride, flags]) + pixels)


def test_deplanarize_is_the_inverse_of_planarize():
    w, rows = 12, 5
    linear = bytes((y * w + x) & 0xFF for y in range(rows) for x in range(w))
    assert deplanarize(planarize(linear, w, rows), w, rows) == linear


def test_planarize_groups_columns_by_x_mod_4_plane_major():
    """Plane p holds columns x%4==p; ALL rows of plane 0 come before plane 1."""
    w, rows = 8, 2
    linear = bytes(range(16))                      # 0..7 / 8..15
    planar = planarize(linear, w, rows)
    # plane 0 = columns 0,4 of each row -> 0,4, 8,12
    assert planar[0:4] == bytes([0, 4, 8, 12])
    # plane 1 = columns 1,5 -> 1,5, 9,13
    assert planar[4:8] == bytes([1, 5, 9, 13])


def test_dos_to_mac_round_trip_is_lossless():
    w, rows = 8, 3
    linear = bytes(range(w * rows))
    dos = _container(b"HLIB", [_tile(b"HLIB", rows, w, planarize(linear, w, rows))])
    mac = convert(dos, to=b"GLIB")
    assert mac[:4] == b"GLIB"
    assert convert(mac, to=b"HLIB") == dos


def test_conversion_deplanarizes_the_pixels_and_rewrites_the_header():
    w, rows = 8, 2
    linear = bytes(range(w * rows))
    dos = _container(b"HLIB", [_tile(b"HLIB", rows, w, planarize(linear, w, rows))])
    mac = parse(convert(dos, to=b"GLIB"))
    ent = mac["entries"][0]
    assert struct.unpack(">H", ent[0:2])[0] == rows
    assert ent[6] == w // 8            # GLIB stores W/8 ...
    assert parse(dos)["entries"][0][6] == w // 4      # ... HLIB stores W/4
    assert ent[7] == 0xC5 and parse(dos)["entries"][0][7] == 0x15
    assert ent[8:] == linear           # pixels are now linear rows


def test_offsets_and_size_survive_the_swap():
    w, rows = 8, 2
    dos = _container(b"HLIB", [_tile(b"HLIB", rows, w, bytes(w * rows))] * 2)
    mac = convert(dos, to=b"GLIB")
    assert parse(mac)["offsets"] == parse(dos)["offsets"]
    assert len(mac) == len(dos)
    assert parse(mac)["size"] == len(mac)


def test_colour_table_payload_passes_through_but_words_swap():
    """rows/stride don't describe the payload -> copy the bytes, swap the words."""
    e = struct.pack("<Hhh", 1, 0, 0) + bytes([0x00, 0x08]) + b"\x98\x00\x97\x33"
    mac = parse(convert(_container(b"HLIB", [e]), to=b"GLIB"))["entries"][0]
    assert struct.unpack(">H", mac[0:2])[0] == 1
    assert mac[6:8] == bytes([0x00, 0x08])      # 0x08: high nibble already 0
    assert mac[8:] == b"\x98\x00\x97\x33"       # payload verbatim


def test_colour_table_flag_byte_IS_remapped_like_any_other_entry():
    """0x18 -> 0xc8.  THE BUG THIS FILE ONCE ASSERTED AS CORRECT.

    The colour table's trailing byte is still an entry flag byte: low nibble =
    block type (8 = palette, per jt993's `(hdr[7] & 15) != 8` check), high nibble
    = format, remapped DOS 0x1 <-> Mac 0xc.

    Every palette in the Yezukriis ground-truth pair is 0x08 -- high nibble
    already 0, so the remap is a no-op and NOT remapping passed the byte-exact
    test.  POR's WALL libraries use 0x18, which must become 0xc8.  That one
    nibble was the ONLY difference between our conversion and the real Mac art:
    9 bytes in 296,922, 7 in 231,434.

    A GROUND-TRUTH TEST ONLY COVERS WHAT THE GROUND TRUTH CONTAINS.
    """
    e = struct.pack("<Hhh", 3, 32, 224) + bytes([0x07, 0x18]) + b"\x11\x22\x33"
    mac = parse(convert(_container(b"HLIB", [e]), to=b"GLIB"))["entries"][0]
    assert mac[7] == 0xC8, "DOS 0x18 must convert to Mac 0xc8"
    assert mac[6] == 0x07                       # cycle-record count untouched
    assert mac[8:] == b"\x11\x22\x33"           # RGB payload verbatim
    # ...and it must survive the trip home.
    dos = parse(convert(convert(_container(b"HLIB", [e]), to=b"GLIB"),
                        to=b"HLIB"))["entries"][0]
    assert dos[7] == 0x18


def test_mac_colour_table_is_recognised_coming_back():
    """0xc8 (200) must be seen as a palette, not rejected as an unknown method.

    The old code keyed off a literal list `(8, 24)`, which does not contain the
    Mac's own 0xc8 -- so a Mac->DOS conversion of any real wall library threw.
    Key off the LOW NIBBLE, which is what the engine itself does.
    """
    e = struct.pack(">Hhh", 3, 32, 224) + bytes([0x07, 0xC8]) + b"\x11\x22\x33"
    dos = parse(convert(_container(b"GLIB", [e]), to=b"HLIB"))["entries"][0]
    assert dos[7] == 0x18


def test_compressed_piece_is_refused_not_mangled():
    w, rows = 8, 2
    ent = _tile(b"HLIB", rows, w, bytes(w * rows), flag_lo=0x2)   # type 2 = RLE
    with pytest.raises(UnsupportedPiece):
        convert(_container(b"HLIB", [ent]), to=b"GLIB")


def test_unknown_flag_byte_is_refused():
    e = struct.pack("<Hhh", 2, 0, 0) + bytes([2, 0x75]) + bytes(16)
    with pytest.raises(UnsupportedPiece):
        convert(_container(b"HLIB", [e]), to=b"GLIB")


def test_rejects_a_non_container():
    with pytest.raises(BadContainer):
        convert(b"NOPE" + bytes(32))


def test_filename_mapping():
    assert mac_name("CPIC1001.TLB") == "cpic1001.ctl"
    assert dos_name("cpic1001.ctl") == "CPIC1001.TLB"


def test_wall_set_names_follow_the_l6eea_id_band_rule():
    # The engine's own wall loader (CODE 7 L6eea) picks the letter from the
    # set-id band -- (id < 10) ? 'b' : 'c' -- so the DOS 8.3 collapse is
    # reversible after all.
    assert mac_name("8X8D1008.TLB") == "8x8db1008.ctl"   # id 8  -> b
    assert mac_name("8X8D1001.TLB") == "8x8db1001.ctl"
    assert mac_name("8X8D3016.TLB") == "8x8dc3016.ctl"   # id 16 -> c
    assert mac_name("8x8d2010.tlb") == "8x8dc2010.ctl"   # boundary: 10 -> c
    assert dos_name("8x8db1008.ctl") == "8X8D1008.TLB"
    assert dos_name("8x8dc3016.ctl") == "8X8D3016.TLB"
    # a letter contradicting the band could not have come from a DOS module
    with pytest.raises(ValueError):
        dos_name("8x8dc1008.ctl")
    with pytest.raises(ValueError):
        dos_name("8x8db1016.ctl")
    # malformed wall names still refuse
    with pytest.raises(ValueError):
        mac_name("8X8D108.TLB")


# --- ground truth ----------------------------------------------------------

YEZU_PC = "data/work/fanmods/yezupc"
YEZU_MAC = "data/work/fanmods/yezu1"


@pytest.mark.skipif(not os.path.isdir(YEZU_MAC),
                    reason="Yezukriis PC/Mac pair not staged (copyrighted, data/ is git-ignored)")
def test_converts_real_dos_art_to_real_mac_art():
    """THE proof: the same module shipped in both formats.

    Two different correctness bars, and the difference matters:
      * UNCOMPRESSED entries must come out BYTE-IDENTICAL to the real Mac file.
      * COMPRESSED entries need only decode to the SAME PIXELS -- our run splits
        are ours, not SSI's (ours land ~0.25% larger).  Byte-equality is not the
        bar for a compressed payload; reproducing the image is.
    """
    macs = sorted(glob.glob(os.path.join(YEZU_MAC, "*.CTL")))
    assert macs, "no Mac art staged"
    exact = pixels = 0
    for mac_path in macs:
        name = os.path.basename(mac_path)[:-4]
        hits = glob.glob(os.path.join(YEZU_PC, "**", name + ".TLB"), recursive=True)
        if not hits:
            continue
        dos = open(hits[0], "rb").read()
        want = open(mac_path, "rb").read()
        got = convert(dos, to=b"GLIB")

        if got == want:
            exact += 1
        else:
            # Must be a compressed piece, and it must decode to the same image.
            g, w = parse(got), parse(want)
            assert g["count"] == w["count"], "%s: entry count changed" % name
            for a, b in zip(g["entries"], w["entries"]):
                if a == b:
                    continue
                assert a[7] == b[7], "%s: drawing method changed" % name
                rows = struct.unpack(">H", a[0:2])[0]
                width = a[6] * 8
                assert (rle_decode(a[8:], width * rows)
                        == rle_decode(b[8:], width * rows)), \
                    "%s: converted art decodes to DIFFERENT pixels" % name
                pixels += 1
    assert exact >= 6, "expected the uncompressed assets byte-exact (got %d)" % exact
    assert pixels == 2, "expected the 2 compressed BIGPIC payloads (got %d)" % pixels


def test_rle_round_trips():
    buf = bytes([7] * 200 + [1, 2, 3] + [9] * 5 + [4, 5])
    assert rle_decode(rle_encode(buf), len(buf)) == buf


def test_rle_uses_repeats_for_runs_of_three_or_more():
    assert rle_encode(bytes([5] * 4)) == bytes([257 - 4, 5])
    # a 2-run costs the same as a literal, so it stays literal
    assert rle_encode(bytes([5, 5])) == bytes([1, 5, 5])


# --- ground truth #2: WALL LIBRARIES (the class Yezukriis did not contain) ---

POR_PC  = "data/work/gamedata/POR.DSN"
POR_MAC = "data/work/fanmods/pormac/POR/GAME39.dsn"


@pytest.mark.skipif(
    not (os.path.isdir(POR_MAC)
         and os.path.exists(os.path.join(POR_PC, "8X8DB.TLB"))),
    reason="Pool of Radiance ground truth not staged — needs BOTH the Mac "
           "wall CTLs (POR_MAC) and the DOS wall TLBs (POR_PC); copyrighted, "
           "data/ is git-ignored. (POR_PC is a manual fixture; `make gamedata` "
           "re-stages only Mac data and wipes it.)")
def test_converts_real_dos_WALL_libraries_to_real_mac_wall_libraries():
    """Pool of Radiance ships in BOTH editions -- same author, same module.

    This is the ground truth the Yezukriis pair could not provide: Yezukriis has
    NO wall art, so every wall-library claim was untested, and a real bug (the
    colour table's flag byte, 0x18 -> 0xc8) sat undetected behind a byte-exact
    test that passed.  8X8DB/8X8DC are container-of-containers -- 10 wall sets x
    48 pieces, each set carrying its own palette -- so they exercise the nested
    path, the per-set colour tables, AND the flag-byte remap in one file.

    Bar: BYTE-IDENTICAL.  528,356 bytes of it.
    """
    for name in ("8X8DB", "8X8DC"):
        dos = open(os.path.join(POR_PC, name + ".TLB"), "rb").read()
        want = open(os.path.join(POR_MAC, name + ".CTL"), "rb").read()
        got = convert(dos, to=b"GLIB")
        assert got == want, (
            "%s.CTL: converted DOS art != the real Mac art (%d bytes differ)"
            % (name, sum(a != b for a, b in zip(got, want))))
        # Round trip: NOT byte-exact back to the DOS file, and that is expected --
        # re-encoding a compressed piece picks our run splits, not SSI's (~0.25%
        # larger).  The invariant that matters is that no INFORMATION is lost:
        # going home and back must land on the same Mac bytes.
        assert convert(convert(got, to=b"HLIB"), to=b"GLIB") == want, \
            "%s: round trip lost information" % name
