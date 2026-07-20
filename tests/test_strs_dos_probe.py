"""Tests for tools/strs_dos_probe.py — Mac STRS vs the DOS CKIT.EXE.

The synthetic tests pin the classification rules: whole-NUL-delimited counts as
recovered, a Mac string that is only a fragment of a longer DOS string does
not, and a missing string is reported absent.

The last test is the one that matters: it runs the probe over the real Mac
resource fork and the real DOS executable and asserts the coverage actually
measured (see docs/dos-strings-probe.md).  Both are copyrighted and live under
the git-ignored data/, so it SKIPS when they are not staged.
"""
import os

import pytest

from strs_dos_probe import build_map, parse_strs, probe, whole_hits

RFORK = "data/work/UnlimitedAdventures.rfork"
CKIT = "data/dos-frua/CKIT.EXE"


def pool(*strings):
    """Build a STRS-style NUL-separated pool."""
    return b"".join(s + b"\x00" for s in strings)


def test_parse_strs_offsets():
    entries = parse_strs(pool(b"alpha", b"be", b"gamma"))
    assert entries == [(0, b"alpha"), (6, b"be"), (9, b"gamma")]


def test_parse_strs_skips_empties():
    # Runs of NULs are padding between entries, not zero-length strings — but
    # each one still advances the pool offset, so "b" sits at 4, not 3.
    assert parse_strs(b"a\x00\x00\x00b\x00") == [(0, b"a"), (4, b"b")]


def test_whole_hits_requires_nul_delimiting():
    exe = b"\x00Podium\x00Pod\x00"
    assert whole_hits(b"Pod", exe) == [8]        # not the one inside "Podium"
    assert whole_hits(b"Podium", exe) == [1]


def test_whole_hits_finds_every_occurrence():
    exe = b"\x00hi\x00hi\x00"
    assert whole_hits(b"hi", exe) == [1, 4]


def test_probe_classifies_three_ways():
    strs = pool(b"present", b"fragment", b"gone")
    exe = b"\x00present\x00xxfragmentxx\x00"
    recovered, substring_only, absent = probe(strs, exe)

    assert [(o, ln) for o, ln, _ in recovered] == [(0, 7)]
    assert substring_only == [(8, b"fragment")]
    assert absent == [(17, b"gone")]


def test_build_map_carries_positions_not_text():
    strs = pool(b"hello")
    exe = b"\x00hello\x00"
    recovered, _, _ = probe(strs, exe)
    table = build_map(recovered)

    assert table == {"0": [1, 5]}
    # The whole point: no copyrighted bytes travel in the table.
    assert b"hello" not in repr(table).encode()


@pytest.mark.skipif(not (os.path.exists(RFORK) and os.path.exists(CKIT)),
                    reason="Mac fork / DOS CKIT.EXE not staged under data/")
def test_real_release_coverage():
    """The measured result — 96.4% recovered, and nothing creative missing."""
    import sys
    sys.path.insert(0, "tools")
    from macrsrc import ResourceFork

    rf = ResourceFork.from_file(RFORK)
    strs = {(r.type, r.id): r.data for r in rf.resources}[("STRS", 0)]
    recovered, substring_only, absent = probe(strs, open(CKIT, "rb").read())
    total = len(recovered) + len(substring_only) + len(absent)

    assert total == 2145
    assert len(recovered) == 2068
    assert len(substring_only) == 40
    assert len(absent) == 37

    # Every absent string is Mac-platform-specific, not game content. Spot-check
    # the markers of that: Mac volume paths, the Mac-only .ctl art templates,
    # and the Mac memory-partition warning (SSI's typo included).
    gone = {s for _, s in absent}
    assert b":DISK4:ALWAYS.CTL" in gone
    assert b"BIGP0%03d.ctl" in gone
    assert b"MacPaint File:" in gone
    assert b"Insufficent Memory" in gone
