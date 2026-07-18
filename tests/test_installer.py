"""The native module installer (installer/main.c) — host-built.

uainst = extract + the SAME byte-exact conversion pipeline the offline
converter runs (colour .ctl twins, mono .tlb synthesis). These tests
build the host CLI and hold its output to the Python reference —
synthetic ZIPs always (CI-safe), the real POR ZIP when staged.
"""
import glob
import os
import struct
import subprocess
import sys
import zipfile

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import art_convert as ac

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "installer", "main.c"),
       os.path.join(ROOT, "installer", "miniz.c"),
       os.path.join(ROOT, "src", "convert", "artconv.c")]


@pytest.fixture(scope="module")
def uainst(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("uainst") / "uainst")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-o", exe] + SRC, check=True)
    return exe


def _tiny_hlib():
    """A minimal one-image HLIB (8x4, uncompressed, plus a palette)."""
    w, rows = 8, 4
    lin = bytes((y * w + x) & 0xFF for y in range(rows) for x in range(w))
    img = struct.pack("<Hhh", rows, 0, 0) + bytes([w // 4, 0x15]) \
        + ac.planarize(lin, w, rows)
    pal = struct.pack("<Hhh", 0, 0, 16) + bytes([0, 0x18]) \
        + bytes(range(48))
    entries = [img, pal]
    off = 16 + 4 * (len(entries) + 1)
    offsets = []
    for e in entries:
        offsets.append(off)
        off += len(e)
    offsets.append(off)
    return (b"HLIB" + struct.pack("<I", off)
            + struct.pack("<H", len(entries)) + b"\0\0" + b"TILE"
            + struct.pack("<%dI" % len(offsets), *offsets)
            + b"".join(entries))


def _run(uainst, zip_path, dest):
    return subprocess.run([uainst, str(zip_path), str(dest)],
                          capture_output=True, text=True)


def test_flat_zip_installs_named_after_the_zip(uainst, tmp_path):
    hlib = _tiny_hlib()
    zp = tmp_path / "mymod.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("BACK1004.TLB", hlib)
        z.writestr("GAME001.DAT", b"\x01\x02design-data\x03")
    dest = tmp_path / "out"
    dest.mkdir()
    r = _run(uainst, zp, dest)
    assert r.returncode == 0, r.stdout + r.stderr
    d = dest / "MYMOD.DSN"
    assert d.is_dir()
    assert (d / "GAME001.DAT").read_bytes() == b"\x01\x02design-data\x03"
    want_ctl = ac.convert(hlib)
    assert (d / "BACK1004.ctl").read_bytes() == want_ctl
    # BACK has a mono family: the .TLB must now be the 1-bit synthesis
    want_mono = ac.mono_synth(want_ctl, ac.mono_family("BACK1004.TLB"))
    assert (d / "BACK1004.TLB").read_bytes() == want_mono


def test_zip_with_dsn_folder_keeps_its_name_and_flattens(uainst, tmp_path):
    hlib = _tiny_hlib()
    zp = tmp_path / "whatever.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Curse.dsn/CPIC1010.TLB", hlib)
        z.writestr("Curse.dsn/sub/README.TXT", b"hi")
    dest = tmp_path / "out"
    dest.mkdir()
    r = _run(uainst, zp, dest)
    assert r.returncode == 0, r.stdout + r.stderr
    d = dest / "Curse.dsn"
    assert (d / "README.TXT").read_bytes() == b"hi"      # flattened
    assert (d / "CPIC1010.ctl").read_bytes() == ac.convert(hlib)


def test_mac_art_in_the_zip_is_kept_verbatim(uainst, tmp_path):
    glib = ac.convert(_tiny_hlib())
    zp = tmp_path / "macmod.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("BACK1004.CTL", glib)
    dest = tmp_path / "out"
    dest.mkdir()
    r = _run(uainst, zp, dest)
    assert r.returncode == 0
    assert (dest / "MACMOD.DSN" / "BACK1004.CTL").read_bytes() == glib


G39_ZIP = "data/work/fanmods/game39.zip"


@pytest.mark.skipif(not os.path.exists(G39_ZIP),
                    reason="POR ZIP not staged; data/ is git-ignored")
def test_real_por_zip_installs_byte_identically(uainst, tmp_path):
    dest = tmp_path / "out"
    dest.mkdir()
    r = _run(uainst, G39_ZIP, dest)
    assert r.returncode == 0, r.stdout[-2000:]
    assert "0 failures" in r.stdout
    d = dest / "Game39.dsn"
    tlbs = sorted(p for p in glob.glob(str(d / "*"))
                  if p.lower().endswith(".tlb"))
    assert len(tlbs) > 180
    # spot-check the heavyweights against the Python pipeline
    for base in ("8X8DB.TLB", "BACK.TLB", "CBODY.TLB"):
        raw = None
        with zipfile.ZipFile(G39_ZIP) as z:
            for n in z.namelist():
                if n.lower().endswith("/" + base.lower()):
                    raw = z.read(n)
        assert raw is not None and raw[:4] == b"HLIB"
        want_ctl = ac.convert(raw)
        assert (d / (base[:-4] + ".ctl")).read_bytes() == want_ctl
        fam = ac.mono_family(base)
        assert (d / base).read_bytes() == ac.mono_synth(want_ctl, fam)
