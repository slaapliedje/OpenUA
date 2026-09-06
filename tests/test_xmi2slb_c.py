"""The C xmi2slb core (src/convert/xmi2slb.c) mirrors tools/xmi2slb.py.

The Python is the byte-exact-tested REFERENCE; the C port is what uainst
links so a fan module's music converts ON THE MACHINE. These hold the two
to IDENTICAL output bytes:

  - synthetic XMI files (always run, incl. CI where data/ is absent) — built
    by hand from the format parse_xmi documents, and chosen to exercise the
    tie rules that a "close enough" port gets wrong: two notes starting on
    the same tick, channel-count ties broken by first appearance, more than
    three channels, percussion on channel 9, a rest gap, a note long enough
    to need tied grid codes, a non-default tempo, an overlap to truncate;
  - the fan-module corpus and the retail set when staged (data/ is
    git-ignored), each module compared bank-for-bank.

The host binary is built on the fly with the host compiler.
"""
import glob
import os
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import xmi2slb as ref  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "convert", "xmi2slb.c"),
       os.path.join(ROOT, "src", "convert", "xmi2slb_main.c")]
FANMODS = os.path.join(ROOT, "data", "work", "fanmods")
RETAIL = os.path.join(ROOT, "data", "work", "dos-run", "DISK1")


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("xmi2slb_c") / "xmi2slb")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-o", exe] + SRC,
                   check=True)
    return exe


# ---- a tiny XMIDI writer, from the format the reference parses ------------

def _vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def _xmi(events, tempo_us=None):
    """events: [(delta_ticks, channel, note, velocity, duration)].
    Deltas are emitted as bare accumulator bytes (0..0x7F each), exactly
    as XMIDI does; Note On carries a VLQ duration and there are no Note Offs."""
    ev = bytearray()
    if tempo_us is not None:
        ev += bytes([0xFF, 0x51]) + _vlq(3) + tempo_us.to_bytes(3, "big")
    for delta, ch, note, vel, dur in events:
        while delta > 0x7F:
            ev.append(0x7F)
            delta -= 0x7F
        if delta:
            ev.append(delta)
        ev += bytes([0x90 | ch, note, vel]) + _vlq(dur)
    ev += bytes([0xFF, 0x2F, 0x00])
    return b"FORM\0\0\0\0XMIDEVNT" + len(ev).to_bytes(4, "big") + bytes(ev)


def _c_bank(cbin, tmp_path, files):
    """files: list of 3 (bytes or None)."""
    args = []
    for i, data in enumerate(files):
        if data is None:
            args.append("-")
        else:
            p = tmp_path / ("q%d.xmi" % (i + 1))
            p.write_bytes(data)
            args.append(str(p))
    out = tmp_path / "out.slb"
    r = subprocess.run([cbin, "bank", str(out)] + args, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return out.read_bytes()


def _py_bank(files):
    return ref.build_bank({i + 1: d for i, d in enumerate(files) if d is not None})


SYNTHETIC = {
    "simple": _xmi([(0, 0, 60, 100, 60), (60, 0, 62, 100, 60), (60, 0, 64, 100, 120)]),
    "same_tick_two_notes": _xmi([(0, 0, 60, 100, 60), (0, 0, 67, 100, 60),
                                 (60, 0, 62, 100, 60)]),
    "overlap_truncates": _xmi([(0, 0, 60, 100, 200), (30, 0, 62, 100, 60)]),
    "rest_gap": _xmi([(0, 0, 60, 100, 30), (200, 0, 62, 100, 30)]),
    "long_note_ties": _xmi([(0, 0, 60, 100, 1000), (1000, 0, 62, 100, 7)]),
    "four_channels_tie_by_appearance": _xmi(
        [(0, 3, 60, 100, 30), (0, 1, 62, 100, 30), (0, 5, 64, 100, 30),
         (30, 2, 65, 100, 30), (30, 3, 67, 100, 30), (30, 1, 69, 100, 30)]),
    "percussion_dropped": _xmi([(0, 9, 36, 100, 30), (0, 0, 60, 100, 30),
                                (30, 9, 38, 100, 30), (30, 0, 62, 100, 30)]),
    "tempo_560747": _xmi([(0, 0, 60, 100, 72), (72, 0, 62, 100, 72)], tempo_us=560747),
    "tempo_half_even": _xmi([(0, 0, 60, 100, 10)], tempo_us=1000000),
    "empty_events": _xmi([]),
}


@pytest.mark.parametrize("name", sorted(SYNTHETIC))
def test_synthetic_song_bytes_match_python(cbin, tmp_path, name):
    data = SYNTHETIC[name]
    assert _c_bank(cbin, tmp_path, [data, None, None]) == _py_bank([data, None, None])


def test_full_and_partial_sets_match_python(cbin, tmp_path):
    a, b, c = SYNTHETIC["simple"], SYNTHETIC["rest_gap"], SYNTHETIC["tempo_560747"]
    assert _c_bank(cbin, tmp_path, [a, b, c]) == _py_bank([a, b, c])
    assert _c_bank(cbin, tmp_path, [None, b, None]) == _py_bank([None, b, None])
    assert _c_bank(cbin, tmp_path, [None, None, None]) == _py_bank([None, None, None])


def test_too_small_arena_fails_cleanly_not_silently(cbin, tmp_path):
    """The arena is caller-supplied and has no allocator behind it, so a file
    with more notes than fit must come back as XMI2SLB_ERR_SPACE — never a
    truncated bank that LOOKS valid, never a write past the buffer. This is
    the case the first cut got wrong: a fixed quarter-arena cap that the two
    Roland modules (3591 note-ons) overran on the host."""
    big = _xmi([(1, 0, 60 + (i % 12), 100, 1) for i in range(5000)])
    p = tmp_path / "big.xmi"
    p.write_bytes(big)
    out = tmp_path / "out.slb"
    r = subprocess.run([cbin, "bank", str(out), str(p), "-", "-", "16384"],
                       capture_output=True, text=True)
    assert r.returncode == 2 and "-3" in r.stderr, (r.returncode, r.stderr)
    assert not out.exists()
    # and with a real arena the same file is byte-identical to the reference
    assert _c_bank(cbin, tmp_path, [big, None, None]) == _py_bank([big, None, None])


def test_sensitivity_a_changed_note_changes_the_bank(cbin, tmp_path):
    """Control: if the C output did not depend on the input, every equality
    above would be vacuous."""
    a = _xmi([(0, 0, 60, 100, 60)])
    b = _xmi([(0, 0, 61, 100, 60)])
    assert _c_bank(cbin, tmp_path, [a, None, None]) != _c_bank(cbin, tmp_path, [b, None, None])


@pytest.mark.parametrize("name,expect", [
    ("ADDQ1.XMI", (3, 1)), ("tydq3.xmi", (0, 3)), ("PCDQ2.XMI", (1, 2)),
    ("RODQ1.XMI", (2, 1)), ("dqkQ2.xmi", (4, 2)), ("Dqk9.xmi", (4, 9)),
    ("DQKX.XMI", None), ("ADDQX.XMI", None), ("XDQ1.XMI", None),
    ("README.TXT", None), ("NOTES.XM", None), ("ZZDQ1.XMI", None),
])
def test_classify_matches_the_python_matcher(cbin, tmp_path, name, expect):
    r = subprocess.run([cbin, "classify", name], capture_output=True, text=True, check=True)
    got = None if r.stdout.strip() == "none" else tuple(int(x) for x in r.stdout.split())
    assert got == expect
    # The reference has no per-NAME function: it walks a directory and only
    # ever SELECTS songs 1..3 (`for q in (1, 2, 3)`), so it cannot answer for
    # Dqk9.xmi even though the name is well-formed — uainst applies the same
    # 1..3 filter after classifying. Cross-check against the reference where
    # it can answer, and only there.
    (tmp_path / name).write_bytes(b"")
    paths, pref = ref.find_xmi_set(str(tmp_path))
    if expect is not None and expect[1] in (1, 2, 3):
        drv = {"TY": 0, "PC": 1, "RO": 2, "AD": 3, "DQK": 4}[pref]
        assert (drv, sorted(paths)[0]) == expect
    elif expect is None:
        assert paths is None


# ---- the corpus, when it is staged ----------------------------------------

def _module_dirs():
    if not os.path.isdir(FANMODS):
        return []
    return sorted(d for d in glob.glob(os.path.join(FANMODS, "*"))
                  if os.path.isdir(d) and ref.find_xmi_set(d)[0])


@pytest.mark.skipif(not os.path.isdir(FANMODS), reason="fan-module corpus not staged")
@pytest.mark.parametrize("moddir", _module_dirs(), ids=os.path.basename)
def test_corpus_module_bank_matches_python(cbin, tmp_path, moddir):
    paths, _ = ref.find_xmi_set(moddir)
    files = [open(paths[q], "rb").read() if q in paths else None for q in (1, 2, 3)]
    assert _c_bank(cbin, tmp_path, files) == _py_bank(files)


@pytest.mark.skipif(not os.path.isdir(RETAIL), reason="retail DOS set not staged")
def test_retail_bank_matches_python_and_prefers_tandy(cbin, tmp_path):
    paths, pref = ref.find_xmi_set(RETAIL)
    assert pref == "TY"
    files = [open(paths[q], "rb").read() for q in (1, 2, 3)]
    assert _c_bank(cbin, tmp_path, files) == _py_bank(files)
