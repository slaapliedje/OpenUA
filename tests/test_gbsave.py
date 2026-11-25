"""src/engine/gbsave.c against tools/gbsave.py: a Dark Queen saved game
(SAVGAMx.QSV) read as the game writes it.

The Python is the reference (ADR-0026). The C must find the same header
fields and the same party records, on synthetic saves (records with and
without affect chains, a full party, the error cases) and on the real saves
when the machine has them: the two the Steam release ships, and any made in
the game under data/work/dqk-saves (git-ignored). The game data is never
committed.
"""
import glob
import os
import random
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbsave                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbsave.c"),
       os.path.join(ROOT, "tests", "gbsave_host.c")]
GAME = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK")
REAL = (sorted(glob.glob(os.path.join(GAME, "SAVGAM?.QSV")))
        + sorted(glob.glob(os.path.join(GAME, "SAVE", "SAVGAM?.QSV")))
        + sorted(glob.glob(os.path.join(ROOT, "data", "work", "dqk-saves",
                                        "*", "SAVGAM?.QSV"))))


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbsave") / "gbsave_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_read(cbin, path):
    out = subprocess.run([cbin, str(path)], check=True, capture_output=True,
                         text=True).stdout.split("\n")
    if out[0].startswith("ERR"):
        return int(out[0].split()[1])
    head = tuple(int(v) for v in out[0].split())
    return head, [bytes.fromhex(line) for line in out[1:] if line]


def py_read(data):
    s = gbsave.GbSave(data)
    return (s.map, s.script, s.x, s.y, s.facing, s.desc, s.backdrop), s.members


def record(rnd, items, nodes, name=b"HERO"):
    r = bytearray(rnd.randrange(256) for _ in range(gbsave.QCH_FIXED))
    r[8] = items
    struct.pack_into("<I", r, 4, rnd.randrange(1, 1 << 32) if nodes else 0)
    r[0x68:0x78] = name.ljust(16, b"\0")
    r += bytes(rnd.randrange(256) for _ in range(gbsave.ITEM * items))
    for k in range(nodes):
        node = bytearray(rnd.randrange(256) for _ in range(gbsave.NODE))
        struct.pack_into("<I", node, 5,
                         0 if k == nodes - 1 else rnd.randrange(1, 1 << 32))
        r += node
    return bytes(r)


def save(rnd, members, cells=None):
    head = bytearray(cells if cells is not None
                     else bytes(rnd.randrange(256) for _ in range(gbsave.CELLS)))
    head += bytes(rnd.randrange(256) for _ in range(gbsave.PARTY - gbsave.CELLS))
    struct.pack_into("<H", head, gbsave.COUNT, len(members))
    body = bytes(head) + b"".join(members) + b"\xff" * 17
    return body + bytes(rnd.randrange(256)
                        for _ in range(max(0, gbsave.SIZE - len(body))))


def test_synthetic_saves_match_reference(cbin, tmp_path):
    rnd = random.Random(26)
    for case in range(12):
        n = case % (gbsave.MEMBERS + 1)
        members = [record(rnd, rnd.randrange(0, 12),
                          rnd.choice((0, 0, 1, 2, 5)), b"M%d" % k)
                   for k in range(n)]
        data = save(rnd, members)
        p = tmp_path / ("s%d.qsv" % case)
        p.write_bytes(data)
        ref = py_read(data)
        assert ref[1] == members
        assert c_read(cbin, p) == ref


def test_header_fields(cbin, tmp_path):
    rnd = random.Random(3)
    cells = bytearray(gbsave.CELLS)
    cells[gbsave.CELL_MAP - 1] = 47
    cells[gbsave.CELL_SCRIPT - 1] = 46
    data = bytearray(save(rnd, [record(rnd, 2, 1)], cells))
    data[gbsave.X], data[gbsave.Y] = 21, 3          # not on the diagonal
    data[gbsave.FACING], data[gbsave.DESC] = 6, 171
    data[gbsave.BACKDROP:gbsave.BACKDROP + 2] = b"\x04\x00"
    p = tmp_path / "h.qsv"
    p.write_bytes(bytes(data))
    head, members = c_read(cbin, p)
    assert head == (47, 46, 21, 3, 3, 171, 4)
    assert py_read(bytes(data))[0] == head
    assert len(members) == 1 and members[0][0x68:0x6a] == b"HE"


def test_errors(cbin, tmp_path):
    rnd = random.Random(5)
    cases = {
        "short": (save(rnd, [])[:gbsave.PARTY - 1], -1),
        "count": (save(rnd, [record(rnd, 0, 0)] * 9), -2),
    }
    endless = bytearray(record(rnd, 1, 3))
    struct.pack_into("<I", endless, len(endless) - gbsave.NODE + 5, 1)
    cases["chain"] = ((save(rnd, [bytes(endless)])[:gbsave.PARTY + len(endless) + 17]), -3)
    cases["past"] = (save(rnd, [record(rnd, 3, 0)])[:gbsave.PARTY + 100], -3)
    for name, (data, code) in cases.items():
        p = tmp_path / (name + ".qsv")
        p.write_bytes(data)
        assert c_read(cbin, p) == code, name
        with pytest.raises(gbsave.GbSaveError):
            gbsave.GbSave(data)


@pytest.mark.skipif(not REAL, reason="no Dark Queen saves on this machine")
def test_real_saves_match_reference(cbin):
    for path in REAL:
        data = open(path, "rb").read()
        assert len(data) == gbsave.SIZE, path
        ref = py_read(data)
        assert c_read(cbin, path) == ref, path
        end = gbsave.PARTY + sum(len(r) for r in ref[1])
        assert data[end:end + 17] == b"\xff" * 17, path   # the records end there


@pytest.mark.skipif(not glob.glob(os.path.join(GAME, "SAVE", "*.QCH")),
                    reason="no Dark Queen heroes on this machine")
def test_shipped_party_is_the_shipped_heroes():
    """The save in SAVE holds the six .QCH heroes beside it; each record is
    its .QCH but for the live pointer words."""
    s = gbsave.GbSave(open(os.path.join(GAME, "SAVE", "SAVGAMA.QSV"), "rb").read())
    heroes = {}
    for f in glob.glob(os.path.join(GAME, "SAVE", "*.QCH")):
        q = open(f, "rb").read()
        heroes[gbsave.name_of(q)] = q
    assert sorted(gbsave.name_of(r) for r in s.members) == sorted(heroes)
    for r in s.members:
        q = heroes[gbsave.name_of(r)]
        assert len(r) == len(q)
        same = sum(a == b for a, b in zip(r, q))
        assert same >= len(q) - 20


# The times dqk-saves/README records for each save, made in the game.
README_TIMES = {"B": (11, 0), "C": (11, 2), "D": (11, 19), "G-edited": (15, 41)}


@pytest.mark.skipif(not REAL, reason="no Dark Queen saves on this machine")
def test_the_clock_is_script_cells_5_to_11():
    base = os.path.join(ROOT, "data", "work", "dqk-saves")
    seen = 0
    for d, hm in README_TIMES.items():
        for path in glob.glob(os.path.join(base, d, "SAVGAM?.QSV")):
            s = gbsave.GbSave(open(path, "rb").read())
            assert s.time_of_day == hm, (d, s.clock)
            seen += 1
    if not seen:
        pytest.skip("the README's saves are not here")
    # B -> C: two minutes on, and day 38 of month 0 carried to day 8 of month 1
    b = gbsave.GbSave(open(glob.glob(os.path.join(base, "B", "*.QSV"))[0], "rb").read())
    c = gbsave.GbSave(open(glob.glob(os.path.join(base, "C", "*.QSV"))[0], "rb").read())
    days = lambda k: k.clock["months"] * gbsave.RADIX[4] + k.clock["days"]
    assert days(b) == days(c) and b.clock["months"] != c.clock["months"]


def test_vault_layout():
    rnd = random.Random(3)
    item = bytes(rnd.randrange(256) for _ in range(gbsave.ITEM))
    v = bytearray(struct.pack("<3I", 7, 1, 2) + b"\xff\xff" + struct.pack("<H", 1)
                  + item + bytes(gbsave.ITEM * (gbsave.VAULT_ITEMS - 1)))
    assert len(v) == gbsave.VAULT_SIZE == 12 + 4 + 17 * 50
    assert gbsave.read_vault(bytes(v)) == ((7, 1, 2), [item])
    with pytest.raises(gbsave.GbSaveError):
        gbsave.read_vault(bytes(v) + b"\0")     # FRUA's is never this size
    v[12] = 0
    with pytest.raises(gbsave.GbSaveError):
        gbsave.read_vault(bytes(v))


@pytest.mark.skipif(not glob.glob(os.path.join(GAME, "SAVE", "VAULT?.DAT")),
                    reason="no Dark Queen vault on this machine")
def test_shipped_vault_is_empty_and_866_bytes():
    for path in glob.glob(os.path.join(GAME, "SAVE", "VAULT?.DAT")):
        money, items = gbsave.read_vault(open(path, "rb").read())
        assert money == (0, 0, 0) and items == []


@pytest.mark.skipif(not REAL, reason="no Dark Queen saves on this machine")
def test_cell_0x20_is_the_party_size():
    # companions counted: save G holds six heroes and an NPC
    for path in REAL:
        s = gbsave.GbSave(open(path, "rb").read())
        assert s.cell(gbsave.CELL_PARTY) == len(s.members), path


def test_the_port_serves_the_games_own_cells():
    import re
    h = open(os.path.join(ROOT, "src", "engine", "gbsave.h")).read()
    for name in ("PLACE", "SEARCH", "PARTY", "MAP"):
        want = getattr(gbsave, "CELL_" + name)
        got = re.search(r"#define GBV_CELL_%s\s+(0x[0-9a-f]+)" % name, h).group(1)
        assert int(got, 16) == want, name
    src = open(os.path.join(ROOT, "src", "engine", "boot.c")).read()
    body = re.search(r"static int gbs_var\(void \*ctx.*?\n\}", src, re.S).group(0)
    for name in ("PLACE", "SEARCH", "PARTY", "MAP"):
        assert "case GBV_CELL_%s" % name in body, name
    assert "[32]" in body and "[25] & 3" in body    # FRUA's h[32] and h[25]


# ---- writing a save back (ADR-0026 decision 4) ----------------------------

def c_write(cbin, path, base=None):
    args = [cbin, "-w", str(path)] + ([str(base)] if base else [])
    out = subprocess.run(args, check=True, capture_output=True,
                         text=True).stdout.strip()
    return bytes.fromhex(out) if not out.startswith("ERR") else out


def test_synthetic_writes_match_reference(cbin, tmp_path):
    rnd = random.Random(44)
    saves = []
    for case in range(8):
        members = [record(rnd, rnd.randrange(0, 10), rnd.choice((0, 1, 3)),
                          b"W%d" % k) for k in range(case % (gbsave.MEMBERS + 1))]
        p = tmp_path / ("w%d.qsv" % case)
        p.write_bytes(save(rnd, members))
        saves.append(p)
    for p in saves:
        s = gbsave.GbSave(p.read_bytes())
        for base in (None, p, saves[(saves.index(p) + 3) % len(saves)]):
            want = gbsave.write_qsv(s.cells, s.x, s.y, s.facing, s.desc,
                                    s.members, base.read_bytes() if base else None,
                                    s.backdrop)
            assert len(want) == gbsave.SIZE
            assert c_write(cbin, p, base) == want, (p, base)


@pytest.mark.skipif(not REAL, reason="no Dark Queen saves on this machine")
def test_real_saves_write_back_byte_for_byte(cbin):
    """Every save the game wrote, rebuilt from its own cells, square and
    records over itself, is that save, in the reference and in the C; over
    another save, the C writes what the reference writes."""
    for path in REAL:
        data = open(path, "rb").read()
        s = gbsave.GbSave(data)
        assert gbsave.write_qsv(s.cells, s.x, s.y, s.facing, s.desc,
                                s.members, data, s.backdrop) == data, path
        assert c_write(cbin, path, path) == data, path
        other = REAL[(REAL.index(path) + 1) % len(REAL)]
        assert c_write(cbin, path, other) == gbsave.write_qsv(
            s.cells, s.x, s.y, s.facing, s.desc, s.members,
            open(other, "rb").read(), s.backdrop), (path, other)


@pytest.mark.skipif(not REAL, reason="no Dark Queen saves on this machine")
def test_backdrop_is_the_areas_art_set():
    """1033 holds LOAD_FILES' second operand for the area saved in: 4 on
    Caergoth (map 4: LOAD_FILES 4,4), 2 in the Draconian Caves (map 10:
    LOAD_FILES 10,2), 0 in a new game's save."""
    by_map = {}
    for path in REAL:
        s = gbsave.GbSave(open(path, "rb").read())
        by_map.setdefault(s.map, set()).add(s.backdrop)
    want = {4: {4}, 10: {2}, 0: {0}}
    for m, v in by_map.items():
        if m in want:
            assert v == want[m], (m, v)
