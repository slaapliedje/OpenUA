"""xmi2slb.find_xmi_set — which XMI files a module's soundtrack is built from.

Deliberately a SEPARATE file from test_xmi2slb.py: that one needs the
copyrighted DOS corpus and skips without it, so on CI the matcher had no
coverage at all. Selection is pure filename logic — nothing here opens a file —
so these run everywhere, which is the point.

They pin the two real fan-module shapes the original 9-character `??DQ?.XMI`
match silently rejected, measured against the module corpus:

    curse    Dqk1.xmi .. Dqk9.xmi   8 chars, no driver pair -> converted to
                                    NOTHING, so the module played the base
                                    game's music and looked fine
    g39      RODQ1.XMI only         a PARTIAL set; requiring all of 1..3 threw
                                    away the one song the module does have
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import xmi2slb  # noqa: E402


def _mk(tmp_path, *names):
    for n in names:
        (tmp_path / n).write_bytes(b"")
    return str(tmp_path)


def test_driver_preference_is_tandy_then_pc_then_roland_then_adlib(tmp_path):
    """Tandy first because SSI already reduced it to three monophonic voices —
    the exact shape the Mac 4-tone sequencer wants. Order matters, so pin it
    rather than trusting the tuple to stay put."""
    d = _mk(tmp_path, *["%sDQ%d.XMI" % (p, q)
                        for p in ("AD", "RO", "PC", "TY") for q in (1, 2, 3)])
    paths, pref = xmi2slb.find_xmi_set(d)
    assert pref == "TY"
    assert sorted(paths) == [1, 2, 3]


def test_falls_back_when_tandy_is_absent(tmp_path):
    # most fan modules ship only AdLib and Roland
    d = _mk(tmp_path, *["%sDQ%d.XMI" % (p, q)
                        for p in ("AD", "RO") for q in (1, 2, 3)])
    _, pref = xmi2slb.find_xmi_set(d)
    assert pref == "RO"


def test_adlib_only_module_still_converts(tmp_path):
    d = _mk(tmp_path, "ADDQ1.XMI", "ADDQ2.XMI", "ADDQ3.XMI")
    paths, pref = xmi2slb.find_xmi_set(d)
    assert pref == "AD" and sorted(paths) == [1, 2, 3]


def test_bare_dqk_family_is_found(tmp_path):
    """curse's shape: `Dqk1.xmi`, 8 characters, no driver pair. The old
    len==9 test rejected every one of them."""
    d = _mk(tmp_path, "Dqk1.xmi", "Dqk3.xmi", "Dqk4.xmi", "Dqk9.xmi")
    paths, pref = xmi2slb.find_xmi_set(d)
    assert pref == "DQK"
    assert sorted(paths) == [1, 3]        # only Q1..Q3 map to Mac slots


def test_dqkQ_family_is_found(tmp_path):
    # wiz1pc's shape: dqkQ1.xmi — DQK driver, Q-numbered
    d = _mk(tmp_path, "dqkQ1.xmi", "dqkQ2.xmi", "dqkQ3.xmi")
    paths, pref = xmi2slb.find_xmi_set(d)
    assert pref == "DQK" and sorted(paths) == [1, 2, 3]


def test_partial_set_converts_what_exists(tmp_path):
    """g39 ships only Q1. Rejecting the module outright lost a song it has."""
    d = _mk(tmp_path, "RODQ1.XMI")
    paths, pref = xmi2slb.find_xmi_set(d)
    assert pref == "RO" and sorted(paths) == [1]


def test_a_directory_with_no_xmi_returns_nothing(tmp_path):
    """Sensitivity control: widening the matcher must not make it match
    anything at all. A miss has to stay a miss, or every module would appear
    to have music."""
    d = _mk(tmp_path, "README.TXT", "GAME.DAT", "PIC1.TLB", "NOTES.XM")
    paths, pref = xmi2slb.find_xmi_set(d)
    assert paths is None and pref is None


def test_near_miss_names_are_not_matched(tmp_path):
    # guard the widened patterns against over-reach
    d = _mk(tmp_path, "DQKX.XMI", "ADDQX.XMI", "DQK.XMI", "XDQ1.XMI")
    paths, _ = xmi2slb.find_xmi_set(d)
    assert paths is None


def test_search_is_recursive_and_case_insensitive(tmp_path):
    # curse keeps its music INSIDE Curse.dsn/, not at the module root
    sub = tmp_path / "Curse.dsn"
    sub.mkdir()
    (sub / "dqk1.XMI").write_bytes(b"")
    (sub / "DqK3.xmi").write_bytes(b"")
    paths, pref = xmi2slb.find_xmi_set(str(tmp_path))
    assert pref == "DQK" and sorted(paths) == [1, 3]


def test_skip_designs_keeps_a_module_out_of_the_ROOT_bank(tmp_path):
    """The walk is recursive, so without pruning a design's soundtrack is a
    candidate for the BASE GAME's bank — and since the DRIVER decides, a module
    shipping an arrangement the retail set lacks would silently replace the
    game's own music. Root stays root."""
    (tmp_path / "ADDQ1.XMI").write_bytes(b"")
    (tmp_path / "ADDQ2.XMI").write_bytes(b"")
    (tmp_path / "ADDQ3.XMI").write_bytes(b"")
    dsn = tmp_path / "Curse.dsn"
    dsn.mkdir()
    for q in (1, 2, 3):
        (dsn / ("TYDQ%d.XMI" % q)).write_bytes(b"")   # Tandy beats AdLib

    # unpruned: the design's Tandy wins the base game's bank — the bug
    _, pref = xmi2slb.find_xmi_set(str(tmp_path))
    assert pref == "TY"

    # pruned: the root keeps its own AdLib set
    paths, pref = xmi2slb.find_xmi_set(str(tmp_path), skip_designs=True)
    assert pref == "AD"
    assert all(".dsn" not in p.lower() for p in paths.values())


def test_skip_designs_does_not_break_a_design_scanned_directly(tmp_path):
    # the per-design pass points AT the .dsn, so pruning must not eat it
    dsn = tmp_path / "Curse.dsn"
    dsn.mkdir()
    (dsn / "Dqk1.xmi").write_bytes(b"")
    paths, pref = xmi2slb.find_xmi_set(str(dsn))
    assert pref == "DQK" and sorted(paths) == [1]
