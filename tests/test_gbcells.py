"""The Dark Queen's character cells (0x401..0x800): tools/gbcells.py against
DQK.EXE, and src/engine/gbcells.c against tools/gbcells.py.

The reference's tables are checked against the switch tables in the game's
own DQK.EXE when the machine has it (never committed; skipped without). The
C file is held to the reference on synthetic records, every cell read and a
long run of random writes. The translation of DQK offsets into the engine's
record is checked through qch.c's own conversion: a field read from a .QCH by
offset must be the field the cell reads from the converted record.
"""
import os
import random
import subprocess
import sys

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gbcells                                                 # noqa: E402
import gbchar                                                  # noqa: E402

DQK_EXE = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK/DQK.EXE")
CELLS = range(gbcells.LO, gbcells.HI + 1)


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbcells") / "gbcells_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe, os.path.join(ROOT, "src", "engine", "gbcells.c"),
                    os.path.join(ROOT, "tests", "gbcells_host.c")], check=True)
    return exe


def crun(cbin, tmp_path, rec, ops):
    p = tmp_path / "rec.bin"
    p.write_bytes(bytes(rec))
    out = []
    # the argument list is long; split it so no command line gets too big
    for i in range(0, max(len(ops), 1), 400):
        part = ops[i:i + 400]
        lines = subprocess.run([cbin, str(p)] + part, check=True,
                               capture_output=True, text=True).stdout.split()
        p.write_bytes(bytes.fromhex(lines[-1]))
        out += lines[:-1]
    return out, bytearray(p.read_bytes())


def random_rec(seed):
    rnd = random.Random(seed)
    return bytearray(rnd.randrange(256) for _ in range(512))


@pytest.mark.skipif(not os.path.exists(DQK_EXE), reason="DQK.EXE not here")
def test_reference_tables_are_dqk_exes():
    rd, wr, spells = gbcells.parse_exe(open(DQK_EXE, "rb").read())
    assert spells == gbcells.SPELLS
    assert rd == gbcells.READ
    assert wr == gbcells.WRITE
    assert len(rd) == 58 and len(wr) == 14


def test_every_cell_reads_as_the_reference(cbin, tmp_path):
    for seed in range(3):
        rec = random_rec(seed)
        out, after = crun(cbin, tmp_path, rec, ["r%x" % c for c in CELLS])
        got = {int(k, 16): int(v, 16) for k, v in (x.split("=") for x in out)}
        want = {c: gbcells.read(rec, c) for c in CELLS}
        assert got == want
        assert after == rec                      # reading changes nothing


def test_random_writes_match_the_reference(cbin, tmp_path):
    rnd = random.Random(7)
    writable = sorted(gbcells.WRITE) + list(range(gbcells.SPELLS[0],
                                                  gbcells.SPELLS[1] + 1))
    rec = random_rec(11)
    ref = bytearray(rec)
    ops = []
    for _ in range(1500):
        c = rnd.choice(writable) if rnd.random() < 0.8 else rnd.choice(CELLS)
        v = rnd.choice([0, 0xff, 1, rnd.randrange(0x10000)])
        ops.append("w%x=%x" % (c, v))
        gbcells.write(ref, c, v)
    _, after = crun(cbin, tmp_path, rec, ops)
    assert after == ref


def test_unmapped_cells_read_zero_and_ignore_writes(cbin, tmp_path):
    rec = random_rec(3)
    out, after = crun(cbin, tmp_path, rec, ["w401=1234", "w4c9=3", "r401",
                                            "r800", "r4ac"])
    assert out == ["401=0", "800=0", "4ac=0"]
    assert after == rec                          # 0x4c9 (order) is read-only


def test_spell_write_follows_dqk():
    rec = bytearray(512)
    first = 198                                  # engine's memorized spells
    rec[first] = 5
    gbcells.write(rec, 0x41f, 0)                 # cast: bit 7, not cleared
    assert rec[first] == 0x85
    gbcells.write(rec, 0x41f, 0)                 # already marked: cleared
    assert rec[first] == 0
    gbcells.write(rec, 0x41f, 0xff)              # 255 means 0
    assert rec[first] == 0
    gbcells.write(rec, 0x41f, 7)
    assert gbcells.read(rec, 0x41f) == 7


def test_money_is_frua_big_endian_words():
    rec = bytearray(512)
    gbcells.write(rec, 0x54c, 1234)              # steel = FRUA's rec[76] word
    assert rec[76:78] == (1234).to_bytes(2, "big")
    assert gbcells.read(rec, 0x54d) == 1234      # both cells name the word
    gbcells.write(rec, 0x550, 0x0102)            # jewelry, rec[80]
    assert rec[80:82] == b"\x01\x02"


def test_status_write_sets_the_dqk_flag():
    rec = bytearray(512)
    gbcells.write(rec, 0x5ee, 0)
    assert rec[94] == 0 and rec[0x17e] == 1
    gbcells.write(rec, 0x5ee, 6)
    assert rec[94] == 6 and rec[0x17e] == 0


def _engine_record(q):
    """The engine's in-memory record for a .QCH: gbchar's .CCH record with
    FRUA's L0ce0 swap applied (longs at 68/72, words at 76..87)."""
    cch, _ = gbchar.convert(bytes(q), native=True)
    r = bytearray(cch[:gbchar.REC])
    for o in (68, 72):
        r[o:o + 4] = r[o:o + 4][::-1]
    for o in range(76, 88, 2):
        r[o:o + 2] = r[o:o + 2][::-1]
    return r + bytearray(512 - len(r))


def _dqk_read(q, cell):
    """What DQK.EXE itself reads at `cell` from its record `q`."""
    if gbcells.SPELLS[0] <= cell <= gbcells.SPELLS[1]:
        return q[gbcells.SPELLS[2] + cell - gbcells.SPELLS[0]]
    if cell not in gbcells.READ:
        return 0
    o, w = gbcells.READ[cell]
    return q[o] | (q[o + 1] << 8 if w == 2 else 0)


def test_cells_read_the_converted_records_fields():
    """Each cell on the engine's record of a converted .QCH gives what DQK
    reads on the .QCH itself, except where FRUA's record has no room."""
    rnd = random.Random(5)
    for seed in range(4):
        q = bytearray(rnd.randrange(256) for _ in range(gbchar.QCH_FIXED))
        q[8] = 0                                 # no items
        q[0x58] = rnd.randrange(8)
        for o in range(0x59, 0x68, 2):           # FRUA keeps these as bytes
            q[o] = 0
        q[0x19a] &= 3
        q[0x19b] &= 7
        q[0x19c] = 0
        q[0x19d] &= 3
        rec = _engine_record(q)
        for c in CELLS:
            if c in gbcells.READ and gbcells.READ[c][0] == 0x5e:
                continue
            o = gbcells.READ.get(c, (gbcells.SPELLS[2] + c - gbcells.SPELLS[0], 1))[0]
            if 0x182 <= o < 0x18b:
                continue
            want = _dqk_read(q, c)
            assert gbcells.read(rec, c) == want, hex(c)
