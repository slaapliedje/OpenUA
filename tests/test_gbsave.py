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


# ---- Champions and Death Knights (gbs1_read, gbsave.Gen1Save) -------------

def gen1_save(dkk, cells, x, y, facing, names, sets=None):
    """A save of either game laid out as tools/gbsave.py's docstring says."""
    width = 1 if dkk else 2
    out = bytearray([1])
    for base, n in gbsave.GEN1_BLOCKS:
        for i in range(n):
            v = cells.get(base + i, 0)
            out += bytes([v & 255]) if dkk else struct.pack("<H", v)
    out += bytes([x, y, facing * 2, 3, 9, 4, 2])
    nsets = 4 if dkk else 3
    for b, s in (sets or [(-1, -1)] * nsets):
        out += struct.pack("<hh", b, s)
    out += bytes([len(names)])
    for k in range(gbsave.GEN1_MEMBERS):
        nm = names[k].encode() if k < len(names) else b""
        slot = bytes([len(nm)]) + nm
        out += slot + bytes([0x5a]) * (gbsave.GEN1_NAME - len(slot))  # junk
    assert len(out) == (gbsave.GEN1_DKK_SIZE if dkk else gbsave.GEN1_COK_SIZE)
    assert width
    return bytes(out)


def c_read1(cbin, path):
    out = subprocess.run([cbin, "-1", str(path)], check=True,
                         capture_output=True, text=True).stdout.split("\n")
    if out[0].startswith("ERR"):
        return out[0]
    head = [int(v) for v in out[0].split()]
    names = out[1:1 + head[-1]]
    cells = {int(a, 16): int(b) for a, b in
             (ln.split() for ln in out[1 + head[-1]:] if ln)}
    return head, names, cells


def py_read1(data):
    g = gbsave.Gen1Save(data)
    head = [int(g.dkk), g.game_area, g.x, g.y, g.facing, g.wall, g.backdrop,
            g.last_state, g.state, len(g.names)]
    return head, g.names, {k: v for k, v in g.cells.items() if v}


@pytest.mark.parametrize("dkk", [False, True])
def test_gen1_synthetic_matches_reference(cbin, tmp_path, dkk):
    rnd = random.Random(7 + dkk)
    cells = {gbsave.GEN1_CELL_SCRIPT: 17, gbsave.GEN1_CELL_MAP: 34,
             0x4bc7: 2, 0x4bc9: 12, 0x7ec6: 75, 0x7a00: 1, 0x7bff: 200,
             0x4eff: 9}
    for _ in range(50):
        cells[rnd.choice((0x4b00, 0x7c00, 0x7a00)) + rnd.randrange(0x200)] = \
            rnd.randrange(256 if dkk else 65536)
    names = ["CHRDATC%d" % i for i in range(1, 7)]
    data = gen1_save(dkk, cells, 7, 13, 1, names)
    p = tmp_path / "s"
    p.write_bytes(data)
    assert c_read1(cbin, p) == py_read1(data)
    g = gbsave.Gen1Save(data)
    assert (g.script, g.map, g.time_of_day) == (17, 34, (12, 2))
    assert (g.x, g.y, g.facing, g.names) == (7, 13, 1, names)
    assert g.cell(0x4eff) == 9 and g.cell(0x7bff) == 200


def test_gen1_wrong_size_and_count(cbin, tmp_path):
    p = tmp_path / "s"
    p.write_bytes(bytes(100))
    assert c_read1(cbin, p) == "ERR -1"
    with pytest.raises(gbsave.GbSaveError):
        gbsave.Gen1Save(bytes(100))
    data = bytearray(gen1_save(False, {}, 0, 0, 0, []))
    data[gbsave.GEN1_COK_SIZE - 1 - 8 * gbsave.GEN1_NAME] = 9
    p.write_bytes(bytes(data))
    assert c_read1(cbin, p) == "ERR -2"


GEN1_REAL = (sorted(glob.glob(os.path.join(ROOT, "data", "work", "cok-saves",
                                           "SAVGAM?.DAT")))
             + sorted(glob.glob(os.path.join(ROOT, "data", "work", "dkk-saves",
                                             "SAVGAM?.PTY")))
             + sorted(glob.glob(os.path.join(ROOT, "data", "work", "gamedata",
                                             "*.DSN", "SAVE", "SAVGAM?.DAT")))
             + sorted(glob.glob(os.path.join(ROOT, "data", "work", "gamedata",
                                             "*.DSN", "SAVE", "SAVGAM?.PTY"))))


@pytest.mark.skipif(not GEN1_REAL, reason="no Champions or Death Knights saves")
def test_gen1_real_saves_match_reference(cbin):
    for path in GEN1_REAL:
        data = open(path, "rb").read()
        if len(data) not in (gbsave.GEN1_COK_SIZE, gbsave.GEN1_DKK_SIZE):
            continue
        assert c_read1(cbin, path) == py_read1(data), path
        g = gbsave.Gen1Save(data)
        letter = os.path.basename(path)[6]
        assert g.names == ["CHRDAT%s%d" % (letter, i)
                           for i in range(1, len(g.names) + 1)], path


SAVE_D = os.path.join(ROOT, "data", "work", "cok-saves", "SAVGAMD.DAT")


@pytest.mark.skipif(not os.path.exists(SAVE_D), reason="no Champions save D")
def test_gen1_champions_save_d():
    """After the caravan fight: back at the outpost (script 17, in town),
    7,13 facing east, 12:02, as the game's own panel showed."""
    g = gbsave.Gen1Save(open(SAVE_D, "rb").read())
    assert (g.dkk, g.script, g.cell(gbsave.GEN1_CELL_DUNGEON)) == (False, 17, 0)
    assert (g.x, g.y, g.facing, g.time_of_day) == (7, 13, 1, (12, 2))
    assert len(g.names) == 6


SAVE_DKK_C = os.path.join(ROOT, "data", "work", "dkk-saves", "SAVGAMC.PTY")


@pytest.mark.skipif(not os.path.exists(SAVE_DKK_C), reason="no Death Knights save C")
def test_gen1_death_knights_save_c():
    """The camp before the Gargath celebration: script 16 has written its
    overland square (33,13), its own number and the rest taken (0x4c04),
    each a byte, as its listing writes them."""
    g = gbsave.Gen1Save(open(SAVE_DKK_C, "rb").read())
    assert (g.dkk, g.width, g.script) == (True, 1, 16)
    assert (g.cell(0x4bc3), g.cell(0x4bc4), g.cell(0x4c04)) == (33, 13, 1)
    assert (g.x, g.y, g.facing, len(g.names)) == (7, 13, 0, 6)


# ---- writing a Champions or Death Knights save (write_gen1, gbs1_write) ---

def c_write1(cbin, path, base, states=None):
    args = [cbin, "-W", str(path), str(base)] + [str(v) for v in (states or ())]
    out = subprocess.run(args, check=True, capture_output=True,
                         text=True).stdout.strip()
    return bytes.fromhex(out) if not out.startswith("ERR") else out


def py_write1(data, base, states=None):
    g = gbsave.Gen1Save(data)
    return gbsave.write_gen1(base, dict(g.cells), g.x, g.y, g.facing, g.wall,
                             g.backdrop, g.names, g.sets, states)


@pytest.mark.parametrize("dkk", [False, True])
def test_gen1_writes_match_reference(cbin, tmp_path, dkk):
    rnd = random.Random(70 + dkk)
    saves = []
    for case in range(5):
        cells = {rnd.choice(range(0x4b00, 0x4f00)): rnd.randrange(1, 256 if dkk else 65536)
                 for _ in range(40)}
        names = ["CHRDATC%d" % (k + 1) for k in range(case + 1)]
        sets = [(rnd.randrange(-1, 30), rnd.randrange(-1, 4)) for _ in range(4 if dkk else 3)]
        p = tmp_path / ("g%d" % case)
        p.write_bytes(gen1_save(dkk, cells, rnd.randrange(16), rnd.randrange(16),
                                rnd.randrange(4), names, sets))
        saves.append(p)
    for p in saves:
        for base in saves:
            assert c_write1(cbin, p, base) == py_write1(p.read_bytes(), base.read_bytes())
            assert c_write1(cbin, p, base, (4, 2)) == \
                py_write1(p.read_bytes(), base.read_bytes(), (4, 2))
        assert py_write1(p.read_bytes(), p.read_bytes()) == p.read_bytes()
    w = gbsave.Gen1Save(py_write1(saves[0].read_bytes(), saves[1].read_bytes(), (4, 2)))
    g = gbsave.Gen1Save(saves[0].read_bytes())
    assert (w.x, w.y, w.facing, w.sets, w.names, w.cells) == \
        (g.x, g.y, g.facing, g.sets, g.names, g.cells)
    assert (w.last_state, w.state) == (4, 2)


def test_gen1_real_saves_write_back_byte_for_byte(cbin):
    paths = [p for p in (SAVE_D, SAVE_DKK_C) if os.path.exists(p)]
    if not paths:
        pytest.skip("no DOSBox saves of either game here")
    for p in paths:
        data = open(p, "rb").read()
        assert py_write1(data, data) == data
        assert c_write1(cbin, p, p) == data
