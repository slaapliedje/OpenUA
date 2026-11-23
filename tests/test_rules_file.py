"""src/engine/rules_file.c against tools/rules.py: a ruleset file
(RULES.DAT), the classes and races a design brings (ADR-0026 step (g)).

The Python is the reference. The C must find the same header, names and
tables on synthetic rulesets of every shape the engine takes, refuse the
same broken files, and read the rulesets rules.py decompiles from FRUA's
resource fork and the Dark Queen's DQK.EXE when the machine has them (the
game data is never committed). The text form must round-trip every file.
"""
import os
import random
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import rules                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "rules_file.c"),
       os.path.join(ROOT, "tests", "rules_file_host.c")]
RFORK = os.path.join(ROOT, "data", "work", "UnlimitedAdventures.rfork")
DQK = os.path.expanduser(
    "~/.local/share/Steam/steamapps/common/Dungeons & Dragons Krynn Series/"
    "games/The Dark Queen of Krynn/GAME/DQK/DQK.EXE")


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("rules") / "rules_file_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def c_read(cbin, path):
    return subprocess.run([cbin, str(path)], check=True, capture_output=True,
                          text=True, encoding="latin-1").stdout


def expected(rs):
    """What rules_file_host prints for the ruleset `rs`."""
    flags = (rules.FLAG_PRIME_LIMITS if rs["prime_limits"] else 0) | \
            (rules.FLAG_KNIGHTS if rs["knights"] else 0)
    L = ["head %d %d %d %d %d %d %d" % (
        rs["races"], rs["progressions"], rs["xp_levels"], rs["kind_row"],
        rs["spell_rows"], rs["default_race"], flags),
        "name " + rs["name"], "coin " + (rs["coin"] or "-"),
        "knam " + "|".join(rs["kind_names"]),
        "rnam " + "|".join(rs["race_names"]),
        "klab " + "|".join(rs["kind_labels"]),
        "rlab " + "|".join(rs["race_labels"])]
    if rs.get("god_names") is not None:
        for tag, (_, field, _) in zip(("sheet", "gods", "robes", "orders"),
                                      rules.SHEET):
            L.append(tag + " " + "|".join(rs[field]))
    for field, tag, _, _, _ in rules.TABLES:
        L.append("%s %s" % (tag, rs[field].hex()))
    sel = {name: (slot, row) for name, slot, row in rs.get("prog_by") or ()}
    for name, _ in rules.SELECTORS:
        if name in sel:
            L.append("sel %d %s" % (sel[name][0], bytes(sel[name][1]).hex()))
        else:
            L.append("sel 255 ")
    return "\n".join(L) + "\n"


def synthetic(rnd, races, progs, levels, krow, spells, coin, select):
    rs = {"name": "Test %d" % rnd.randrange(1000), "coin": coin,
          "races": races, "progressions": progs, "xp_levels": levels,
          "kind_row": krow, "spell_rows": spells,
          "default_race": rnd.randrange(races),
          "prime_limits": rnd.randrange(2), "knights": rnd.randrange(2)}
    word = lambda: "".join(rnd.choice("ABCDEFGHIJ-/ ") for _ in range(rnd.randrange(1, 16)))
    rs["kind_names"] = [word() for _ in range(rules.KINDS)]
    rs["kind_labels"] = [word().upper() for _ in range(rules.KINDS)]
    rs["race_names"] = [word() for _ in range(races + 1)]
    rs["race_labels"] = [word().upper() for _ in range(races)]
    rs["slot_names"] = [word() for _ in range(rules.SLOTS)]
    rs["prog_names"] = [word() for _ in range(progs)]
    for field, _, el, rows, cols in rules.TABLES:
        n = rows(rs) * cols(rs) * rules.WIDTH[el]
        rs[field] = bytes(rnd.randrange(256) for _ in range(n))
    rs["race_base"] = bytes(rnd.randrange(7) for _ in range(races))
    rs["slot_spell_row"] = bytes(rnd.choice([255, rnd.randrange(spells)])
                                 for _ in range(progs))
    rs["slot_prog"] = bytes(rnd.randrange(progs) for _ in range(rules.SLOTS))
    rk = bytearray()
    for _ in range(races):
        n = rnd.randrange(krow)
        row = [n] + [rnd.randrange(rules.KINDS) for _ in range(n)]
        rk += bytes(row + [0] * (krow - len(row)))
    rs["race_kinds"] = bytes(rk)
    if select:
        for _, field, n in rules.SHEET:
            rs[field] = [word() for _ in range(n)]
        rs["prog_by"] = [(name, rnd.randrange(rules.SLOTS),
                          [rnd.randrange(progs) for _ in range(n)])
                         for name, n in rules.SELECTORS if rnd.randrange(3)]
    return rs


SHAPES = [(6, 7, 12, 14, 4, None, False),
          (7, 11, 19, 15, 8, "Steel", True),
          (1, 1, 2, 2, 1, "X", True),
          (7, 16, 29, 18, 16, None, True),
          (3, 9, 20, 5, 2, "Silver Piece", False)]


@pytest.mark.parametrize("shape", SHAPES)
def test_synthetic_rulesets_match_reference(cbin, tmp_path, shape):
    rnd = random.Random(hash(shape) & 0xffff)
    rs = synthetic(rnd, *shape)
    data = rules.to_bytes(rs)
    p = tmp_path / "RULES.DAT"
    p.write_bytes(data)
    back = rules.from_bytes(data)
    assert c_read(cbin, p) == expected(back)
    # every table sits on a long boundary of the file (a 68000 needs it)
    q = 8
    while q < len(data):
        n = int.from_bytes(data[q + 4:q + 6], "big")
        assert (q + 8) % 4 == 0
        q += 8 + n + (-n % 4)
    # the text round-trips the file exactly
    assert rules.to_bytes(rules.from_text(rules.to_text(back))) == data


def broken(rs, tag, fn):
    """The file for `rs` with chunk `tag`'s data passed through fn (None
    drops the chunk)."""
    data = rules.to_bytes(rs)
    out, q = bytearray(data[:8]), 8
    while q < len(data):
        t = data[q:q + 4]
        n = int.from_bytes(data[q + 4:q + 6], "big")
        body = data[q + 8:q + 8 + n]
        q += 8 + n + (-n % 4)
        if t == tag.encode():
            if fn is None:
                continue
            body = fn(bytearray(body))
        out += t + len(body).to_bytes(2, "big") + b"\0\0" + bytes(body) \
            + b"\0" * (-len(body) % 4)
    return bytes(out)


def test_errors(cbin, tmp_path):
    rnd = random.Random(7)
    rs = synthetic(rnd, 7, 11, 19, 15, 8, "Steel", True)

    def put(i, v):
        def f(b):
            b[i] = v
            return b
        return f

    cases = {
        "magic": (b"UARULES2" + rules.to_bytes(rs)[8:], -1, "-"),
        "chunk": (rules.to_bytes(rs)[:-3], -2, None),
        "races": (broken(rs, "HEAD", put(0, 8)), -3, "HEAD"),
        "default": (broken(rs, "HEAD", put(5, 7)), -3, "HEAD"),
        "no table": (broken(rs, "THAC", None), -4, "THAC"),
        "no names": (broken(rs, "KNAM", None), -4, "KNAM"),
        "size": (broken(rs, "SAVE", lambda b: b[:-1]), -5, "SAVE"),
        "names": (broken(rs, "RNAM", lambda b: b + b"Extra\0"), -6, "RNAM"),
        "unended": (broken(rs, "NAME", lambda b: b[:-1]), -6, "NAME"),
        "base": (broken(rs, "RBAS", put(0, 7)), -7, "RBAS"),
        "spell row": (broken(rs, "SROW", put(0, 8)), -7, "SROW"),
        "slot prog": (broken(rs, "SPRG", put(0, 11)), -7, "SPRG"),
        "kinds": (broken(rs, "RKND", put(1, 17)), -7, "RKND"),
        "count": (broken(rs, "RKND", put(0, 15)), -7, "RKND"),
        "select": (broken(rs, "PSEL", put(1, 11)), -7, "PSEL"),
        "robe": (broken(rs, "PSEL", lambda b: bytes(b[:14]) + bytes([5, 0, 0, 0, 11])),
                 -7, "PSEL"),
    }
    for name, (data, code, what) in cases.items():
        if name == "kinds" and rs["race_kinds"][0] == 0:
            data = broken(rs, "RKND", lambda b: bytearray([1, 17]) + b[2:])
        p = tmp_path / (name.replace(" ", "_") + ".dat")
        p.write_bytes(data)
        out = c_read(cbin, p).split()
        assert out[:2] == ["ERR", str(code)], name
        if what is not None:
            assert out[2] == what, name
        with pytest.raises(rules.RulesError):
            rules.from_bytes(data)


def test_text_errors():
    rnd = random.Random(9)
    text = rules.to_text(synthetic(rnd, 6, 7, 12, 14, 4, None, False))
    lines = text.splitlines()
    for bad in (
        [l for l in lines if not l.startswith("slot_thac0 3 ")],      # a row
        [l.replace("slot_thac0 3 ", "slot_thac0 4 ") for l in lines],  # order
        [l.replace("thief_skills 0  ", "thief_skills 0  1 ") for l in lines],
        [l for l in lines if not l.startswith("monster ")],
        [l for l in lines if not l.startswith("race 2 ")],
        lines + ["warp 9"],
        [l.replace("race_start_age 0  ", "race_start_age 0  1d4 ")
         if l.startswith("race_start_age 0 ") else l for l in lines],
    ):
        with pytest.raises(rules.RulesError):
            rules.from_text("\n".join(bad))


def test_kind_list_is_carried_not_derived():
    """kind_list() (rules.c's, for DQK.EXE) gives a flagged kind the NEXT
    offered kind's row. FRUA's L3666 gives its cleric/thief the row before
    (cleric/magic-user's), so a file carries its class list rather than
    have the engine derive it; all else agrees."""
    if not os.path.exists(RFORK):
        pytest.skip("Mac resource fork not staged under data/")
    rs = rules.from_frua(RFORK)
    got = rules.kind_list(rs["race_kinds"], 6, 14)
    diff = [k for k in range(rules.KINDS) if got[k] != rules.FRUA_KIND_LIST[k]]
    assert diff == [12]
    assert got[12] == 10 | 0x80 and rules.FRUA_KIND_LIST[12] == 9 | 0x80


@pytest.mark.skipif(not os.path.exists(RFORK),
                    reason="Mac resource fork not staged under data/")
def test_frua_ruleset(cbin, tmp_path):
    rs = rules.from_frua(RFORK)
    assert rs["race_names"] == ["Elf", "Half-Elf", "Dwarf", "Gnome",
                                "Halfling", "Human", "Monster"]
    assert rs["race_labels"][1] == "HALF-ELF"
    assert rs["kind_labels"][16] == "MAGIC-USER/THIEF"
    data = rules.to_bytes(rs)
    p = tmp_path / "FRUA.DAT"
    p.write_bytes(data)
    assert c_read(cbin, p) == expected(rs)
    assert rules.to_bytes(rules.from_text(rules.to_text(rs))) == data


@pytest.mark.skipif(not os.path.exists(DQK), reason="no DQK.EXE on this machine")
def test_dqk_ruleset(cbin, tmp_path):
    import gbrules
    exe = open(DQK, "rb").read()
    rs = rules.from_dqk(exe)
    t = gbrules.read_dqk(exe)
    assert rs["slot_xp"] == t["xp"] and rs["spell_slots"] == t["spell_slots"]
    assert rs["coin"] == "Steel" and rs["race_labels"][0] == "SILVANESTI ELF"
    assert rs["god_names"][3] == "Kiri-Jolith" and rs["order_names"][2] == " of the Sword"
    data = rules.to_bytes(rs)
    p = tmp_path / "KRYNN.DAT"
    p.write_bytes(data)
    assert c_read(cbin, p) == expected(rs)
    assert rules.to_bytes(rules.from_text(rules.to_text(rs))) == data
