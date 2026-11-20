"""tools/gbrules.py and src/engine/rules_gb.c: Dark Queen's rules out of DQK.EXE.

The Python file is the reference (ADR-0026); the C loader is built on the
host and must produce the same engine tables, byte for byte:

  * on a SYNTHETIC executable: every table position filled with its own
    pattern and the names pointed at strings of our own, so a misplaced
    table, a wrong stride or a missed byte swap shows as a difference;
  * on the REAL DQK.EXE when the machine has it (the Steam release), with
    the sums checked, plus the facts the Adventurer's Journal prints.

The game data is never committed; the real-file checks skip without it.
"""
import glob
import os
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbrules                                                 # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "rules_gb.c"),
       os.path.join(ROOT, "tests", "rules_gb_host.c")]

KINDS = ["Kind%d" % k for k in range(gbrules.KINDS)]
RACES = ["Race %c" % (65 + k) for k in range(gbrules.RACES)] + ["Monster"]


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("rules_gb") / "rules_gb_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def synthetic():
    """An executable-shaped file: each table's bytes are (seed + i * 7) so
    no two tables or offsets look alike; names at DS:0x7000."""
    exe = bytearray(gbrules.DS + 0x7400)
    for seed, (name, off, n, _) in enumerate(gbrules.TABLES):
        if name in gbrules.NAME_TABLES:
            continue
        p = gbrules.DS + off
        exe[p:p + n] = bytes((seed * 31 + i * 7 + 1) & 0xff for i in range(n))
    sp = 0x7000
    for name, strings in (("kind_names", KINDS), ("race_names", RACES)):
        _, off, _, _ = gbrules._table(name)
        for k, s in enumerate(strings):
            struct.pack_into("<HH", exe, gbrules.DS + off + 4 * k, sp, gbrules.DS_SEG)
            b = s.encode() + b"\0"
            exe[gbrules.DS + sp:gbrules.DS + sp + len(b)] = b
            sp += len(b)
    return bytes(exe)


def c_tables(cbin, path, verify=False):
    r = subprocess.run([cbin, path] + (["verify"] if verify else []),
                       capture_output=True, text=True, check=True)
    lines = r.stdout.splitlines()
    if lines[0] != "OK":
        return lines[0], None
    t = {"kind_names": [], "race_names": []}
    for ln in lines[1:]:
        if ln.startswith("name "):
            _, field, _, text = ln.split(" ", 3)
            t[field].append(text)
        else:
            field, h = ln.split(" ")
            t[field] = bytes.fromhex(h)
    return "OK", t


def py_tables(exe, check=False):
    return gbrules.read_dqk(exe, check=check)


def test_synthetic_matches_reference(cbin, tmp_path):
    exe = synthetic()
    p = tmp_path / "SYN.EXE"
    p.write_bytes(exe)
    status, ct = c_tables(cbin, str(p))
    assert status == "OK"
    pt = py_tables(exe)
    assert set(ct) == set(pt)
    for field in pt:
        assert ct[field] == pt[field], field
    assert ct["kind_names"] == KINDS and ct["race_names"] == RACES


def test_byte_order_and_strides():
    """The conversions themselves, on bytes we chose."""
    exe = bytearray(synthetic())
    put = lambda name, rel, b: exe.__setitem__(           # noqa: E731
        slice(gbrules.DS + gbrules._table(name)[1] + rel,
              gbrules.DS + gbrules._table(name)[1] + rel + len(b)), b)
    put("xp", 4 * gbrules.XP_LEVELS, struct.pack("<l", 3000))      # prog 1, level 0
    put("race_age_cat", 10, struct.pack("<H", 650))                # race 1, cat 0
    put("race_start_age", 28 + 4, struct.pack("<HBB", 130, 5, 6))  # race 1, slot 1
    put("race_kinds", 29, bytes([14]) + struct.pack("<14H", *range(1, 15)))
    put("kind_aligns", 21, bytes([3]) + struct.pack("<10H", 0, 3, 6, 9, 9, 9, 9, 9, 9, 9))
    t = gbrules.engine_tables(bytes(exe))
    assert t["xp"][4 * gbrules.XP_LEVELS:4 * gbrules.XP_LEVELS + 4] == struct.pack(">l", 3000)
    assert t["race_age_cat"][10:12] == struct.pack(">H", 650)
    assert t["race_start_age"][32:36] == struct.pack(">HBB", 130, 5, 6)
    assert t["race_kinds"][15:30] == bytes([14] + list(range(1, 15)))
    assert t["kind_aligns"][12:24] == bytes([3, 0, 3, 6, 9, 9, 9, 9, 9, 9, 9, 0])


def test_refuses_another_build(cbin, tmp_path):
    p = tmp_path / "SYN.EXE"
    p.write_bytes(synthetic())
    status, _ = c_tables(cbin, str(p), verify=True)
    assert status.startswith("ERR -2 ")
    with pytest.raises(gbrules.GbRulesError):
        gbrules.read_dqk(synthetic())


def test_bad_name_pointer(cbin, tmp_path):
    exe = bytearray(synthetic())
    off = gbrules.DS + gbrules._table("race_names")[1]
    struct.pack_into("<H", exe, off + 2, 0x1234)                   # not the DS
    p = tmp_path / "SYN.EXE"
    p.write_bytes(bytes(exe))
    assert c_tables(cbin, str(p))[0] == "ERR -3 race_names"
    with pytest.raises(gbrules.GbRulesError):
        gbrules.engine_tables(bytes(exe))


def _real_exe():
    steam = os.path.expanduser("~/.local/share/Steam/steamapps/common")
    for pat in (os.path.join(ROOT, "data", "**", "DQK.EXE"),
                os.path.join(steam, "Dungeons & Dragons Krynn Series", "**", "DQK.EXE")):
        for p in sorted(glob.glob(pat, recursive=True)):
            return p
    pytest.skip("no DQK.EXE on this machine")


def test_real_exe_matches_reference(cbin):
    path = _real_exe()
    with open(path, "rb") as fh:
        exe = fh.read()
    status, ct = c_tables(cbin, path, verify=True)
    assert status == "OK"
    pt = py_tables(exe, check=True)
    for field in pt:
        assert ct[field] == pt[field], field


def test_real_exe_says_what_the_journal_says():
    with open(_real_exe(), "rb") as fh:
        t = gbrules.read_dqk(fh.read())
    assert t["race_names"] == ["Silvanesti Elf", "Qualinesti Elf", "Half-Elf",
                               "Mountain Dwarf", "Hill Dwarf", "Kender", "Human",
                               "Monster"]
    assert t["kind_names"][:7] == ["Cleric", "Knight", "Fighter", "Paladin",
                                   "Ranger", "Magic-User", "Thief"]
    # Maximum Level Limits by Race and Class (human knight 18, kender cleric 12)
    cap = t["race_level_cap"]
    assert cap[6 * 7:7 * 7] == bytes([40, 18, 40, 40, 40, 40, 40])
    assert cap[5 * 7:6 * 7] == bytes([12, 0, 5, 0, 5, 0, 40])
    # Level Advancement Tables: level 2 of each progression, and the step
    xp = lambda p, lv: struct.unpack_from(">l", t["xp"], 4 * (gbrules.XP_LEVELS * p + lv))[0]  # noqa: E731
    step = lambda p: struct.unpack_from(">l", t["xp_step"], 4 * p)[0]                          # noqa: E731
    level2 = [4000, 3000, 2001, 5000, 5000, 5000, 2251, 5000, 5000, 1251, 2751]
    assert [xp(p, 2) for p in range(gbrules.PROGS)] == level2
    assert (xp(4, 3), xp(5, 4)) == (12000, 27000)        # Sword, Rose
    assert (step(0), step(1), step(7), step(8), step(9)) == (250000, 225000, 250000, 200000, 220000)
    # Knights and paladins are lawful good; nobody may be evil
    al = t["kind_aligns"]
    assert al[12:14] == bytes([1, 0]) and al[36:38] == bytes([1, 0])
    for k in range(gbrules.KINDS):
        assert not {2, 5, 8} & set(al[12 * k + 1:12 * k + 1 + al[12 * k]])
    # A human may be any of the seven single classes; a half-elf, 14 kinds
    assert t["race_kinds"][6 * 15] == 7 and t["race_kinds"][2 * 15] == 14
