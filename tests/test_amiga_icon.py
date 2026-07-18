"""Validate tools/make_amiga_icon.py by parsing the DiskObject it writes back
with an independent reader — magic, type, stack, image geometry, tooltypes."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import make_amiga_icon as mki


def parse_diskobject(data):
    """Minimal DiskObject reader: returns a dict of the fields the icon
    carries. Independent of the writer's own packing."""
    assert len(data) >= 78
    magic, version = struct.unpack_from(">HH", data, 0)
    # gadget: width/height at offsets 0x0c/0x0e, render flags at 0x16/0x1a
    ga_width, ga_height = struct.unpack_from(">HH", data, 0x0c)
    ga_render, ga_select = struct.unpack_from(">II", data, 0x16)
    do_type, _pad = struct.unpack_from(">BB", data, 0x30)
    do_default, do_tooltypes = struct.unpack_from(">II", data, 0x32)
    do_currx, do_curry = struct.unpack_from(">II", data, 0x3a)
    do_stack = struct.unpack_from(">I", data, 0x4a)[0]

    off = 0x4e
    img = None
    if ga_render:
        le, te, w, h, depth, idata, ppick, ponoff, nxt = struct.unpack_from(
            ">hhHHHIBBI", data, off)
        img = dict(w=w, h=h, depth=depth)
        words_per_row = (w + 15) // 16
        nplanes = bin(ppick).count("1")
        off += 20 + words_per_row * h * nplanes * 2
    if do_default:
        n = struct.unpack_from(">I", data, off)[0]
        off += 4 + n
    tooltypes = []
    if do_tooltypes:
        size = struct.unpack_from(">I", data, off)[0]
        off += 4
        for _ in range(size // 4 - 1):
            n = struct.unpack_from(">I", data, off)[0]
            off += 4
            tooltypes.append(data[off:off + n - 1].decode("latin-1"))
            off += n
    return dict(magic=magic, version=version, type=do_type, stack=do_stack,
                render=ga_render, gadget_w=ga_width, gadget_h=ga_height,
                currx=do_currx, curry=do_curry, image=img, tooltypes=tooltypes)


def test_uainst_icon_fields():
    data = mki.build_icon(icon_type=mki.WB_TOOL, stack=200000,
                          tooltypes=["STACK=200000"])
    d = parse_diskobject(data)
    assert d["magic"] == 0xE310
    assert d["version"] == 1
    assert d["type"] == mki.WB_TOOL
    assert d["stack"] == 200000
    assert d["render"] != 0                     # has a gadget image
    assert d["tooltypes"] == ["STACK=200000"]


def test_image_geometry_matches_gadget():
    data = mki.build_icon(tooltypes=["STACK=200000"])
    d = parse_diskobject(data)
    # the gadget's width/height must match the embedded image
    assert d["image"] is not None
    assert d["gadget_w"] == d["image"]["w"]
    assert d["gadget_h"] == d["image"]["h"]
    assert d["image"]["depth"] == 2


def test_multiple_tooltypes_roundtrip():
    tts = ["STACK=200000", "DONOTWAIT", "CX_PRIORITY=0"]
    d = parse_diskobject(mki.build_icon(tooltypes=tts))
    assert d["tooltypes"] == tts


def test_ascii_glyph_dimensions():
    glyph = mki.parse_glyph("####\n#..#\n####\n")
    assert glyph is not None
    d = parse_diskobject(mki.build_icon(glyph=glyph, tooltypes=["STACK=1"]))
    assert d["image"]["w"] == 4 and d["image"]["h"] == 3


def test_fixed_position_encoded():
    d = parse_diskobject(mki.build_icon(tooltypes=["STACK=1"], curr_x=12, curr_y=8))
    assert d["currx"] == 12
    assert d["curry"] == 8


def test_default_position_is_no_icon_position():
    d = parse_diskobject(mki.build_icon(tooltypes=["STACK=1"]))
    assert d["currx"] == mki.NO_ICON_POSITION
    assert d["curry"] == mki.NO_ICON_POSITION


def test_stack_field_offset_matches_real_layout():
    # do_StackSize is the last LONG of the 78-byte header (verified against a
    # stock WB Tools.info where it read 0x1000). Guard that offset here.
    data = mki.build_icon(stack=0xABCD, tooltypes=["STACK=1"])
    assert struct.unpack_from(">I", data, 0x4a)[0] == 0xABCD
