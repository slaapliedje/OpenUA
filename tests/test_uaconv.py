"""uaconv (installer/uaconv.c) — the Amiga bulk converter, host-built.

Until 2026-09-05 this tool had NO tests at all, although its header says the
convert path is host-testable. These build it with the host compiler (the
directory scan is the only platform split; POSIX readdir stands in for
dos.library) and hold it to the behaviours a hand-copied install relies on:

  - the art pass still converts a DOS .tlb to its .ctl twin (the thing the
    tool existed for — touched here, so pinned here);
  - the music pass writes MUSIC.SLB in the ROOT and in each .DSN from the
    .XMI it finds, byte-identical to the Python reference;
  - an EXISTING MUSIC.SLB is never overwritten (a Mac 8-song root bank must
    not become a 3-song DOS conversion);
  - no .XMI -> no bank and no "music:" line (the control).
"""
import os
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import art_convert as ac  # noqa: E402
import xmi2slb as xref  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "installer", "uaconv.c"),
       os.path.join(ROOT, "src", "convert", "artconv.c"),
       os.path.join(ROOT, "src", "convert", "xmi2slb.c")]


@pytest.fixture(scope="module")
def uaconv(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("uaconv") / "uaconv")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-I" + os.path.join(ROOT, "installer"),
                    "-o", exe] + SRC, check=True)
    return exe


def _run(uaconv, d):
    # `-d` answers the delete prompt for it: no interactive stdin under pytest
    return subprocess.run([uaconv, "-d", str(d)], capture_output=True, text=True)


def _tiny_hlib():
    w, rows = 8, 4
    lin = bytes((y * w + x) & 0xFF for y in range(rows) for x in range(w))
    img = struct.pack("<Hhh", rows, 0, 0) + bytes([w // 4, 0x15]) + ac.planarize(lin, w, rows)
    pal = struct.pack("<Hhh", 0, 0, 16) + bytes([0, 0x18]) + bytes(range(48))
    entries = [img, pal]
    off = 16 + 4 * (len(entries) + 1)
    offsets = []
    for e in entries:
        offsets.append(off)
        off += len(e)
    offsets.append(off)
    return (b"HLIB" + struct.pack("<I", off) + struct.pack("<H", len(entries)) + b"\0\0"
            + b"TILE" + struct.pack("<%dI" % len(offsets), *offsets) + b"".join(entries))


def _tiny_xmi(events, tempo_us=None):
    def vlq(v):
        out = [v & 0x7F]
        v >>= 7
        while v:
            out.append((v & 0x7F) | 0x80)
            v >>= 7
        return bytes(reversed(out))
    ev = bytearray()
    if tempo_us is not None:
        ev += bytes([0xFF, 0x51]) + vlq(3) + tempo_us.to_bytes(3, "big")
    for delta, ch, note, vel, dur in events:
        if delta:
            ev.append(delta)
        ev += bytes([0x90 | ch, note, vel]) + vlq(dur)
    ev += bytes([0xFF, 0x2F, 0x00])
    return b"FORM\0\0\0\0XMIDEVNT" + len(ev).to_bytes(4, "big") + bytes(ev)


Q1 = _tiny_xmi([(0, 0, 60, 100, 60), (60, 0, 64, 100, 60)], tempo_us=560747)
Q2 = _tiny_xmi([(0, 1, 62, 100, 30)])
Q3 = _tiny_xmi([(0, 2, 67, 100, 120)])


def test_art_pass_still_converts_a_tlb(uaconv, tmp_path):
    (tmp_path / "PIC.TLB").write_bytes(_tiny_hlib())
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0, r.stdout + r.stderr
    assert (tmp_path / "PIC.ctl").read_bytes() == ac.convert(_tiny_hlib())
    assert "Converted 1 file" in r.stdout


def test_root_and_each_design_get_their_own_bank(uaconv, tmp_path):
    (tmp_path / "TYDQ1.XMI").write_bytes(Q1)          # a hand-copied DOS root
    (tmp_path / "TYDQ2.XMI").write_bytes(Q2)
    (tmp_path / "TYDQ3.XMI").write_bytes(Q3)
    d = tmp_path / "Mod.dsn"
    d.mkdir()
    (d / "Dqk1.xmi").write_bytes(Q3)                  # a module's DQK family
    (d / "Dqk3.xmi").write_bytes(Q1)
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0, r.stdout + r.stderr
    assert (tmp_path / "MUSIC.SLB").read_bytes() == xref.build_bank({1: Q1, 2: Q2, 3: Q3})
    assert (d / "MUSIC.SLB").read_bytes() == xref.build_bank({1: Q3, 3: Q1})
    assert "Tandy" in r.stdout and "DQK" in r.stdout and "partial" in r.stdout
    assert "Wrote 2 MUSIC.SLB" in r.stdout


def test_design_music_does_not_leak_into_the_root(uaconv, tmp_path):
    """The root scan is NON-recursive: a module's Tandy set must not become
    the base game's bank (the trap stage_dos hit with --skip-designs)."""
    d = tmp_path / "Mod.dsn"
    d.mkdir()
    (d / "TYDQ1.XMI").write_bytes(Q1)
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0
    assert not (tmp_path / "MUSIC.SLB").exists()
    assert (d / "MUSIC.SLB").exists()


def test_existing_bank_is_kept_byte_for_byte(uaconv, tmp_path):
    """A Mac root bank has eight songs; a DOS conversion has three. Never
    replace one with the other."""
    mac = b"SLBR" + b"\x00" * 60                    # stands in for the real bank
    (tmp_path / "MUSIC.SLB").write_bytes(mac)
    (tmp_path / "TYDQ1.XMI").write_bytes(Q1)
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0
    assert (tmp_path / "MUSIC.SLB").read_bytes() == mac
    assert "kept" in r.stdout
    assert "Wrote" not in r.stdout


def test_driver_preference_and_q_filter(uaconv, tmp_path):
    (tmp_path / "ADDQ1.XMI").write_bytes(Q1)          # AdLib set, complete
    (tmp_path / "ADDQ2.XMI").write_bytes(Q2)
    (tmp_path / "ADDQ3.XMI").write_bytes(Q3)
    (tmp_path / "PCDQ2.XMI").write_bytes(Q3)          # PC speaker outranks AdLib
    (tmp_path / "Dqk9.xmi").write_bytes(Q1)           # q=9 never maps to a slot
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0
    assert (tmp_path / "MUSIC.SLB").read_bytes() == xref.build_bank({2: Q3})
    assert "PC speaker" in r.stdout and "1 of 3" in r.stdout


def test_no_xmi_means_no_bank_and_no_music_line(uaconv, tmp_path):
    (tmp_path / "GAME.DAT").write_bytes(b"\0" * 16)
    d = tmp_path / "Quiet.dsn"
    d.mkdir()
    (d / "GAME.DAT").write_bytes(b"\0" * 16)
    r = _run(uaconv, tmp_path)
    assert r.returncode == 0
    assert not (tmp_path / "MUSIC.SLB").exists()
    assert not (d / "MUSIC.SLB").exists()
    assert "music:" not in r.stdout
    assert "Nothing to convert" in r.stdout
