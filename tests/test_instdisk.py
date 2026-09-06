"""instdisk (installer/instdisk.c) — the manifest-driven data-disk copier, host-built.

It had no host test until 2026-09-05. What is pinned is the thing a field
report asked for: when it finishes, the LAST lines on screen say exactly where
the files went — the design's final path, and a warning when the destination
had to be CREATED (the default DH0:OpenUA is made if absent, so on a machine
whose real OpenUA drawer lives elsewhere a module quietly lands in a fresh,
empty-looking drawer on DH0:).

A one-disk manifest needs no disk swap, so the console build runs end to end
with no stdin.
"""
import os
import subprocess
import sys

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "installer", "instdisk.c")]


@pytest.fixture(scope="module")
def instdisk(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("instdisk") / "instdisk")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-o", exe] + SRC, check=True)
    return exe


def _disk(tmp_path, title, files):
    src = tmp_path / "disk1"
    src.mkdir()
    (src / "DISK.LST").write_text("1 1 %s\n%s\n" % (title, "\n".join(files)))
    for f in files:
        p = src / f
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"\xAB" * 100)
    return src


def _run(instdisk, dest, src):
    return subprocess.run([instdisk, str(dest), str(src)], capture_output=True,
                          text=True, stdin=subprocess.DEVNULL)


def test_module_set_names_the_design_and_its_final_path(instdisk, tmp_path):
    src = _disk(tmp_path, "Curse module", ["Curse.dsn/GAME.DAT", "Curse.dsn/MUSIC.SLB"])
    dest = tmp_path / "OpenUA"
    dest.mkdir()                                   # an EXISTING drawer
    r = _run(instdisk, dest, src)
    assert r.returncode == 0, r.stdout
    assert (dest / "Curse.dsn" / "MUSIC.SLB").read_bytes() == b"\xAB" * 100
    tail = r.stdout.strip().splitlines()[-1]
    assert "The design Curse.dsn is now in" in tail and str(dest) in tail
    assert "SELECT A DESIGN" in tail
    assert "CREATED" not in r.stdout                # it existed: no warning


def test_created_destination_is_called_out_last(instdisk, tmp_path):
    src = _disk(tmp_path, "Curse module", ["Curse.dsn/GAME.DAT"])
    dest = tmp_path / "NewDrawer"                  # does NOT exist yet
    r = _run(instdisk, dest, src)
    assert r.returncode == 0, r.stdout
    assert (dest / "Curse.dsn" / "GAME.DAT").exists()
    out = r.stdout
    assert "did not exist before and was CREATED" in out
    assert "move Curse.dsn into it" in out
    # the warning is after the file list, not buried above it
    assert out.index("CREATED") > out.index("Curse.dsn/GAME.DAT")


def test_base_game_set_has_no_design_line(instdisk, tmp_path):
    """Control: root files of the base game are not a design, so the design
    line must not appear — otherwise it would name something meaningless."""
    src = _disk(tmp_path, "OpenUA game data", ["FRAME.TLB", "HEIRS.DSN/GAME.DAT"])
    dest = tmp_path / "OpenUA"
    dest.mkdir()
    r = _run(instdisk, dest, src)
    assert r.returncode == 0, r.stdout
    # the first manifest path is a ROOT file, so no design is named
    assert "The design" not in r.stdout
    assert (dest / "FRAME.TLB").exists() and (dest / "HEIRS.DSN" / "GAME.DAT").exists()


def test_backslashes_are_normalised_to_slashes(instdisk, tmp_path):
    """The field case: "Personal:Games\\OpenUA\\" typed from a DOS habit
    installed into a drawer literally NAMED "Games\\OpenUA\\". On the Amiga
    (and the host) '\\' is a filename character, not a separator, so the
    installer turns it into '/', strips the trailing one, and says so."""
    src = _disk(tmp_path, "Curse module", ["Curse.dsn/GAME.DAT"])
    games = tmp_path / "Games"
    games.mkdir()
    typed = str(tmp_path) + "\\Games\\OpenUA\\"          # what a DOS hand types
    r = _run(instdisk, typed, src)
    assert r.returncode == 0, r.stdout
    assert (games / "OpenUA" / "Curse.dsn" / "GAME.DAT").exists()
    assert not any("\\" in p.name for p in tmp_path.iterdir())   # no drawer named with a backslash
    assert "backslashes are not path separators" in r.stdout
    assert "The design Curse.dsn is now in " + str(games / "OpenUA") in r.stdout
