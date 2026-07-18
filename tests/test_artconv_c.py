"""The C artconv core (src/convert/artconv.c) mirrors tools/art_convert.py.

The Python is the byte-exact-tested REFERENCE; the C port is what the
engine (in-engine on-load conversion) and the native installer link. These
tests hold the two to IDENTICAL output bytes:

  - synthetic containers (always run, incl. CI where data/ is absent);
  - the full DOS fan-art corpus when it is staged (data/ is git-ignored).

The host binary is built on the fly with the host compiler — no cross
toolchain needed.
"""
import glob
import os
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import art_convert as ac

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "convert", "artconv.c"),
       os.path.join(ROOT, "src", "convert", "artconv_main.c")]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("artconv_c") / "artconv")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-o", exe] + SRC, check=True)
    return exe


def _c_conv(cbin, tmp_path, data, mode="conv", dosname=None):
    src = tmp_path / "in.bin"
    dst = tmp_path / "out.bin"
    src.write_bytes(data)
    cmd = [cbin, mode, str(src), str(dst)]
    if dosname:
        cmd.append(dosname)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr
    return dst.read_bytes(), None


def _container(magic, entries, tag=b"TILE"):
    e = ">" if magic == b"GLIB" else "<"
    off = 16 + 4 * (len(entries) + 1)
    offsets = []
    for x in entries:
        offsets.append(off)
        off += len(x)
    offsets.append(off)
    return (magic + struct.pack(e + "I", off) + struct.pack(e + "H", len(entries))
            + b"\0\0" + tag
            + struct.pack(e + "%dI" % len(offsets), *offsets) + b"".join(entries))


def _synthetic_hlib():
    """One container exercising every entry kind the corpus contains."""
    w, rows = 8, 4
    lin = bytes((y * w + x) & 0xFF for y in range(rows) for x in range(w))
    plain = struct.pack("<Hhh", rows, 1, -2) + bytes([w // 4, 0x15]) \
        + ac.planarize(lin, w, rows)
    pair = struct.pack("<Hhh", rows, 0, 0) + bytes([w // 4, 0x11]) \
        + ac.planarize(bytes([0xFF] * (w * rows)), w, rows) \
        + ac.planarize(lin, w, rows)
    rle = struct.pack("<Hhh", rows, 0, 0) + bytes([w // 4, 0x12]) \
        + ac.rle_encode(ac._rows_interleave(lin, w, rows))
    px = bytearray(w * rows)
    mask = bytearray(w * rows)
    for y in range(rows):
        for x in range(1, w - 1):
            px[y * w + x] = 0x40 + x
            mask[y * w + x] = 1
    m23 = struct.pack("<Hhh", rows, 0, 0) + bytes([w // 4, 0x17]) \
        + ac.m23_encode(px, mask, w, rows, planar=True)
    frames = struct.pack("<Hhh", 2, 0, 0) + bytes([1, 0x19]) + bytes(range(12))
    t128 = struct.pack("<Hhh", 3, 0, 0) + bytes([0x80, 0x00]) \
        + struct.pack("<8H", *range(8))
    pal = struct.pack("<Hhh", 0, 0, 16) + bytes([0, 0x18]) \
        + bytes(range(48))
    stub = b"\0\0"
    return _container(b"HLIB",
                      [plain, pair, rle, m23, frames, t128, pal, stub])


def test_c_matches_python_on_a_synthetic_container(cbin, tmp_path):
    data = _synthetic_hlib()
    py = ac.convert(data)
    c, err = _c_conv(cbin, tmp_path, data)
    assert err is None, err
    assert c == py
    # and the reverse direction, from the converted GLIB
    c2, err = _c_conv(cbin, tmp_path, py)
    assert err is None, err
    assert c2 == ac.convert(py)


def test_c_matches_python_on_a_nested_master_library(cbin, tmp_path):
    sub = _synthetic_hlib()
    idtab = struct.pack("<8H", *range(8))
    data = _container(b"HLIB", [idtab, sub, sub], tag=b"HLIB")
    py = ac.convert(data)
    c, err = _c_conv(cbin, tmp_path, data)
    assert err is None, err
    assert c == py


def test_c_matches_python_on_mono_synthesis(cbin, tmp_path):
    glib = ac.convert(_synthetic_hlib())
    for name in ("BACK1004.TLB", "CPIC1010.TLB", "8X8D1001.TLB",
                 "BIGP0244.TLB"):
        fam = ac.mono_family(name)
        py = ac.mono_synth(glib, fam)
        c, err = _c_conv(cbin, tmp_path, glib, "mono", name)
        assert err is None, err
        assert c == py, name


def test_c_mono_family_mapping_matches_python(cbin):
    # exercised through the CLI's family resolution in the mono tests;
    # here just pin the no-family stems: the CLI must refuse them.
    for name in ("FRAME.TLB", "TITLE.TLB", "MENU.TLB"):
        assert ac.mono_family(name) is None


def test_c_refuses_a_non_container(cbin, tmp_path):
    c, err = _c_conv(cbin, tmp_path, b"JUNKJUNKJUNKJUNKJUNK")
    assert c is None


G39_DOS = "data/work/fanmods/g39/Game39.dsn"


@pytest.mark.skipif(not os.path.isdir(G39_DOS),
                    reason="POR corpus not staged; data/ is git-ignored")
def test_c_matches_python_over_the_full_por_corpus(cbin, tmp_path):
    files = sorted(glob.glob(os.path.join(G39_DOS, "*.TLB"))
                   + glob.glob(os.path.join(G39_DOS, "*.tlb")))
    assert len(files) > 100
    checked = mono_checked = 0
    for path in files:
        data = open(path, "rb").read()
        if data[:4] != b"HLIB":
            continue
        py = ac.convert(data)
        c, err = _c_conv(cbin, tmp_path, data)
        assert err is None, (path, err)
        assert c == py, path
        checked += 1
        fam = ac.mono_family(path)
        if fam is None:
            continue
        pym = ac.mono_synth(py, fam)
        cm, err = _c_conv(cbin, tmp_path, py, "mono", os.path.basename(path))
        assert err is None, (path, err)
        assert cm == pym, path
        mono_checked += 1
    assert checked >= 190 and mono_checked >= 188
