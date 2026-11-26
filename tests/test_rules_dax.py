"""tools/gbrules.py read_dax and src/engine/rules_gb.c gbr_load_dax:
Champions' and Death Knights' rules out of START.EXE and GAME.OVR.

The Python reader is the reference (ADR-0026); the C loader must give the
same engine tables byte for byte, on synthetic files (every table position
filled with noise, names of our own, the image EXEPACKed here) and on the
real games when the machine has them, where the facts the Journals and the
games' own code give are checked too. The game data is never committed.
"""
import os
import random
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbrules                                                 # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "rules_gb.c"),
       os.path.join(ROOT, "src", "engine", "gbdaxmon.c"),
       os.path.join(ROOT, "tests", "rules_gb_host.c")]
GAMES = os.path.join(ROOT, "data", "work", "gamedata")
FOLDER = {"cok": "COKRAW.DSN", "dkk": "DKKRAW.DSN"}


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("rules_dax") / "rules_gb_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def exepack(image):
    """`image` (16-byte paragraphs) as an EXEPACK program: zero runs as
    fill records, the rest as copies of at most 0x8000 bytes, the first
    record marked last (they are run from the end)."""
    runs, k = [], 0
    while k < len(image):
        z = k
        while z < len(image) and image[z] == 0 and z - k < 0x8000:
            z += 1
        if z - k >= 64:
            runs.append((k, z, True))
            k = z
            continue
        e = k
        while e < len(image) and e - k < 0x8000 and image[e:e + 64] != bytes(64):
            e += 1
        e = max(e, k + 1)
        runs.append((k, e, False))
        k = e
    stream = b""
    for i, (a, b, fill) in enumerate(runs):
        last = 1 if i == 0 else 0
        if fill:
            stream += b"\0" + struct.pack("<H", b - a) + bytes([0xb0 | last])
        else:
            stream += image[a:b] + struct.pack("<H", b - a) + bytes([0xb2 | last])
    stream += b"\xff" * (-len(stream) % 16)
    head = struct.pack("<8H", 0, 0, 0, 0, 0, 0, len(image) // 16, 0) + b"RB"
    img = stream + head + b"\0" * 14
    size = 32 + len(img)
    mz = bytearray(32)
    mz[0:2] = b"MZ"
    struct.pack_into("<HHHH", mz, 2, size % 512, (size + 511) // 512, 0, 2)
    struct.pack_into("<H", mz, 0x16, len(stream) // 16)
    return bytes(mz) + img


def pascal_table(strings, stride):
    out = bytearray()
    for s in strings:
        b = s.encode()
        out += bytes([len(b)]) + b + bytes(stride - 1 - len(b))
    return bytes(out)


def synthetic(game, seed):
    """A load image and a GAME.OVR for `game`: noise in every table, our
    own names, sane race rows."""
    rng = random.Random(seed)
    g = gbrules.DAX_GAMES[game]
    img = bytearray(g["image"])
    for name, off, size, _ in g["tables"]:
        p = g["ds"] + off
        img[p:p + size] = bytes(rng.randrange(256) for _ in range(size))
        if name in gbrules.DAX_PASCAL:
            stride, count = gbrules.DAX_PASCAL[name]
            img[p:p + size] = pascal_table(
                ["%s %d" % (name[:4], k) for k in range(count)], stride)
        elif name == "race_kinds":
            for r in range(gbrules.RACES):
                n = rng.randrange(15)
                img[p + 15 * r] = n
                for k in range(14):
                    img[p + 15 * r + 1 + k] = rng.randrange(17) if k < n else 0
        elif name == "align_index":
            img[p:p + size] = bytes(rng.randrange(9) for _ in range(size))
    ovr = bytearray(rng.randrange(256) for _ in range(g["orders"][0] + 64))
    orders = b"".join(bytes([len(s)]) + s for s in (b" of A", b" of Bee", b" of Cs"))
    ovr[g["orders"][0]:g["orders"][0] + len(orders)] = orders
    return bytes(img), bytes(ovr)


def c_tables(cbin, start, ovr, verify=False):
    r = subprocess.run([cbin, "dax", str(start), str(ovr)] + (["verify"] if verify else []),
                       capture_output=True, text=True, check=True)
    lines = r.stdout.splitlines()
    if lines[0] != "OK":
        return lines[0], None
    t = {"kind_names": [], "race_names": [], "money_names": [], "race_labels": [],
         "kind_labels": [], "sheet_kind_names": [], "god_names": [],
         "robe_names": [], "order_names": []}
    for ln in lines[2:]:
        if ln.startswith("name "):
            _, field, _, text = (ln.split(" ", 3) + [""])[:4]
            t[field].append(text)
        else:
            field, h = ln.split(" ")
            t[field] = bytes.fromhex(h)
    t["game"] = int(lines[1].split()[1])
    return "OK", t


def compare(ct, pt):
    for field, v in pt.items():
        if field == "title":
            continue
        assert ct[field] == v, field


@pytest.mark.parametrize("game", ["cok", "dkk"])
def test_synthetic_matches_reference(cbin, tmp_path, game):
    for seed in range(3):
        img, ovr = synthetic(game, seed * 7 + len(game))
        start = tmp_path / "START.EXE"
        start.write_bytes(exepack(img))
        o = tmp_path / "GAME.OVR"
        o.write_bytes(ovr)
        status, ct = c_tables(cbin, start, o)
        assert status == "OK"
        assert ct["game"] == (1 if game == "cok" else 2)
        compare(ct, gbrules.read_dax(img, ovr, game, check=False))


def test_refuses_another_build(cbin, tmp_path):
    img, ovr = synthetic("dkk", 1)
    (tmp_path / "START.EXE").write_bytes(exepack(img))
    (tmp_path / "GAME.OVR").write_bytes(ovr)
    status, _ = c_tables(cbin, tmp_path / "START.EXE", tmp_path / "GAME.OVR", verify=True)
    assert status == "ERR -2 slot_thac0"
    with pytest.raises(gbrules.GbRulesError):
        gbrules.read_dax(img, ovr, "dkk")
    (tmp_path / "START.EXE").write_bytes(exepack(img[:-16]))
    status, _ = c_tables(cbin, tmp_path / "START.EXE", tmp_path / "GAME.OVR")
    assert status == "ERR -4 -"


def test_kind_and_slot_moves():
    """The Knight is the game's slot and kind 7, the engine's 1."""
    img, ovr = synthetic("dkk", 5)
    g = gbrules.DAX_GAMES["dkk"]
    t = gbrules.read_dax(img, ovr, "dkk", check=False)
    raw = lambda n: gbrules.dax_raw(img, "dkk", n)               # noqa: E731
    assert t["slot_hd_size"][1] == raw("slot_hd_size")[7]
    assert t["slot_thac0"][22:22 + 19] == raw("slot_thac0")[7 * 19:8 * 19]
    assert t["kind_ability_min"][6:12] == raw("kind_ability_min")[42:48]
    assert t["kind_names"][1] == "kind 7" and t["kind_names"][7] == "kind 1"
    assert g["top"] == 18


def _real(game):
    folder = os.path.join(GAMES, FOLDER[game])
    if not os.path.exists(os.path.join(folder, "START.EXE")):
        pytest.skip("no %s here" % FOLDER[game])
    return folder


@pytest.mark.parametrize("game", ["cok", "dkk"])
def test_real_game_matches_reference(cbin, game):
    folder = _real(game)
    status, ct = c_tables(cbin, os.path.join(folder, "START.EXE"),
                          os.path.join(folder, "GAME.OVR"), verify=True)
    assert status == "OK"
    compare(ct, gbrules.read_dax_folder(folder))


def xp(t, p, lv):
    return struct.unpack_from(">l", t["xp"], 4 * (gbrules.XP_LEVELS * p + lv))[0]


def test_real_death_knights():
    t = gbrules.read_dax_folder(_real("dkk"))
    assert t["kind_names"][:8] == ["Cleric", "Knight", "Fighter", "Paladin",
                                   "Ranger", "Mage", "Thief", "Druid"]
    assert t["money_names"] == ["Steel", "Gems", "Jewelry"]
    assert t["order_names"] == ["", " of the Crown", " of the Sword", " of the Rose"]
    assert t["god_names"][3] == "Kiri-Jolith" and t["robe_names"] == ["", "White", "Red", "Black"]
    cap = t["race_level_cap"]
    assert cap[6 * 7:7 * 7] == bytes([14, 14, 14, 14, 14, 14, 18])   # human
    assert cap[5 * 7:6 * 7] == bytes([12, 0, 5, 0, 5, 0, 18])        # kender
    # the Dark Queen's progressions to level 15, nothing past it but a thief
    level2 = [4000, 3000, 2001, 5000, 5000, 5000, 2251, 5000, 5000, 1251, 2751]
    assert [xp(t, p, 2) for p in range(gbrules.PROGS)] == level2
    assert xp(t, 0, 14) == 1500000 and xp(t, 0, 15) == gbrules.NEVER
    assert xp(t, 9, 18) == 1760001
    # a knight: 2d10 at first level, THAC0 from the game's slot 7
    assert (t["slot_hd_count"][1], t["slot_hd_size"][1]) == (2, 10)
    assert t["slot_thac0"][22:22 + 4] == bytes([40, 40, 40, 42])
    # every cleric one first-level spell at level 1 (OVR 0x3616f)
    assert t["spell_slots"][0] == 1 and t["spell_slots"][261] == 1
    # knights and paladins lawful good, nobody evil
    al = t["kind_aligns"]
    assert al[12:14] == bytes([1, 0]) and al[36:38] == bytes([1, 0])
    for k in range(gbrules.KINDS):
        assert not {2, 5, 8} & set(al[12 * k + 1:12 * k + 1 + al[12 * k]])


def test_real_champions():
    t = gbrules.read_dax_folder(_real("cok"))
    # no paladin progression; nothing past level 12 in any table
    assert all(xp(t, 10, lv) == gbrules.NEVER for lv in range(2, gbrules.XP_LEVELS))
    assert xp(t, 0, 8) == 160000 and xp(t, 0, 9) == gbrules.NEVER
    assert xp(t, 9, 9) == 110001                         # a thief to 9
    cap = t["race_level_cap"]
    # Champions' Journal: human cleric 8, fighter 8, ranger 7, mage 8,
    # thief 9; Kender fighter and ranger 7 at best (OVR 0x25061)
    assert cap[6 * 7:7 * 7] == bytes([8, 8, 8, 0, 7, 8, 9])
    assert cap[5 * 7:6 * 7] == bytes([8, 0, 7, 0, 7, 0, 9])
    assert cap[0 * 7 + 6] == 0                           # no Silvanesti thief
    # 5 spell levels: a level-8 white robe has no 5th
    row = t["spell_row"][7]
    assert t["spell_slots"][261 * row + 9 * 7 + 4] == 0
    assert t["slot_thac0"][22 * 2 + 12] == t["slot_thac0"][22 * 2 + 21]
