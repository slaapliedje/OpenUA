"""src/engine/gbdaxmon.c against tools/gbdaxmon.py: Champions' and Death
Knights' monsters and items.

The Python functions are the reference (ADR-0026). The C reader must give the
same bytes on synthetic data (an EXEPACK program made here, items of every
kind the translation treats apart, characters of both games with and without
spells) and on the real games when the machine has them. The game data is
never committed.
"""
import glob
import os
import random
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import gbdaxmon as G                                           # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = [os.path.join(ROOT, "src", "engine", "gbdaxmon.c"),
       os.path.join(ROOT, "tests", "gbdaxmon_host.c")]
GAMES = os.path.join(ROOT, "data", "work", "gamedata")


@pytest.fixture(scope="module")
def cbin(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("gbdaxmon") / "gbdaxmon_host")
    subprocess.run(["cc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-o", exe] + SRC, check=True)
    return exe


def run(cbin, *args):
    return subprocess.run([cbin] + [str(a) for a in args], check=True,
                          capture_output=True, text=True).stdout.splitlines()


def write(tmp_path, name, data):
    p = tmp_path / name
    p.write_bytes(data)
    return p


# ---- a synthetic EXEPACK program ----------------------------------------

def exepack(image):
    """`image` (a multiple of 16 bytes) as an EXEPACK program: its first
    part copied, a run of one byte filled, its last part copied."""
    assert len(image) % 16 == 0
    third = len(image) // 3
    a, b = image[:third], image[2 * third:]
    fill = len(image) - len(a) - len(b)
    image = a + bytes([0x55]) * fill + b
    stream = (a + struct.pack("<H", len(a)) + b"\xb3"
              + b"\x55" + struct.pack("<H", fill) + b"\xb0"
              + b + struct.pack("<H", len(b)) + b"\xb2")
    stream += b"\xff" * (-len(stream) % 16)
    cs = len(stream) // 16
    head = struct.pack("<8H", 0, 0, 0, 0, 0, 0, len(image) // 16, 0) + b"RB"
    img = stream + head + b"\0" * 14
    size = 32 + len(img)
    mz = bytearray(32)
    mz[0:2] = b"MZ"
    struct.pack_into("<HHHH", mz, 2, size % 512, (size + 511) // 512, 0, 2)
    struct.pack_into("<H", mz, 0x16, cs)
    return bytes(mz) + img, image


def word_image():
    words = ["Battle Axe"] + ["Word %d" % k for k in range(2, 126)]
    body = b"".join(bytes([len(w)]) + w.encode() + b"\0" * (20 - len(w)) for w in words)
    image = b"\x07" * 100 + body + b"\x09" * 50
    return image + b"\0" * (-len(image) % 16), words


def test_unexepack_and_words(cbin, tmp_path):
    image, words = word_image()
    exe, unpacked = exepack(image)
    assert G.unexepack(exe) == unpacked
    p = write(tmp_path, "START.EXE", exe)
    # the fill ran over part of the words: read them from what unpacks
    try:
        ref = G.game_words(exe)[1:]
    except G.GbDaxMonError:
        ref = None
    out = run(cbin, "words", p)
    if ref is None:
        assert out[0].startswith("ERR")
    else:
        assert out == ref


def test_words_survive_packing(cbin, tmp_path):
    image, words = word_image()
    image = image + b"\0" * (2 * len(image))   # the words in the copied start
    image += b"\0" * (-len(image) % 16)
    exe, _ = exepack(image)
    p = write(tmp_path, "START.EXE", exe)
    ref = G.game_words(exe)[1:]
    assert ref == words
    assert run(cbin, "words", p) == ref


def test_word_sources(cbin):
    for n in range(120, 160):
        want = next((k for k, to in G.WORD_MAP.items() if to == n), 0)
        assert run(cbin, "source", n) == [str(want)]


# ---- items ----------------------------------------------------------------

def some_items(rng, n=300):
    out = []
    for i in range(n):
        t = bytearray(rng.randrange(256) for _ in range(17))
        t[0] = rng.choice([39, 40, 73, 0x34, 5, 18, 47, 52, rng.randrange(128)])
        if i % 3 == 0:
            t[16] = 0x80
        t[1] = rng.choice([0, 12, 67, 69, 124, 125, t[1]])
        out.append(bytes(t))
    return out


def test_items_match_reference(cbin, tmp_path):
    items = some_items(random.Random(7))
    p = write(tmp_path, "items.bin", b"".join(items))
    assert [bytes.fromhex(x) for x in run(cbin, "items", p)] == [G.item17(t) for t in items]


def test_table_matches_reference(cbin, tmp_path):
    rng = random.Random(11)
    first = [G.item17(t) for t in some_items(rng, 40)]
    first += first[:5]                              # put: kept in place
    rest = [G.item17(t) for t in some_items(rng, 120)]
    rest += [q[:10] + bytes([q[10] ^ 0x80]) + q[11:] for q in rest[:30]]  # readied only
    a = write(tmp_path, "a.bin", b"".join(first))
    b = write(tmp_path, "b.bin", b"".join(rest))
    out = run(cbin, "table", "=%s" % a, b)
    cut = out.index("--")
    ref = [G.table_key(q) for q in first]
    seen = set(ref)
    for q in rest:
        k = G.table_key(q)
        if k not in seen and len(ref) < G.ITEMS_MAX:
            seen.add(k)
            ref.append(k)
    assert [bytes.fromhex(x) for x in out[:cut]] == ref
    index = {}
    for k, q in enumerate(ref):
        index.setdefault(q, k + 1)
    assert [int(x) for x in out[cut + 1:]] == [index[G.table_key(q)] for q in first + rest]


# ---- monsters -------------------------------------------------------------

def c_member(cbin, tmp_path, cha, wiz=None, spc=b"", itm=b""):
    files = []
    for name, data in (("cha", cha), ("wiz", wiz), ("spc", spc), ("itm", itm)):
        files.append(write(tmp_path, name, data) if data else "-")
    out = run(cbin, "monster", *files)
    return out[0] if out[0].startswith("ERR") else bytes.fromhex(out[0])


def ref_member(cha, wiz=None, spc=b"", itm=b""):
    if len(cha) >= G.COK_SIZE:
        k, mem = G.cok_to_dkk(cha)
    else:
        k, mem = cha, (wiz[:G.WIZ_MEMORIZED] if wiz else None)
    items = [G.item17(r[G.ITEM_TAIL:G.ITEM_TAIL + 17]) for r in G.records(itm)]
    return G.dqk_member(G.dkk_to_dqk(k), mem, G.effects(spc), items)


@pytest.mark.parametrize("size,cls", [(409, 3), (409, 2), (216, 0), (216, 7)])
def test_synthetic_monsters_match_reference(cbin, tmp_path, size, cls):
    rng = random.Random(size * 31 + cls)
    for _ in range(6):
        cha = bytearray(rng.randrange(256) for _ in range(size))
        cha[0] = rng.randrange(20)
        if size == 409:
            cha[0x5b] = cls                     # Death Knights' 0x21
        else:
            cha[0x21] = cls
        wiz = bytes(rng.randrange(140) for _ in range(220)) if size == 216 else None
        spc = bytes(rng.randrange(256) for _ in range(9 * rng.randrange(5)))
        itm = b"".join(bytes(42) + bytes(4) + t for t in some_items(rng, rng.randrange(4)))
        assert c_member(cbin, tmp_path, bytes(cha), wiz, spc, itm) == \
            ref_member(bytes(cha), wiz, spc, itm)


def test_short_character(cbin, tmp_path):
    assert c_member(cbin, tmp_path, bytes(100)) == "ERR -1"


# ---- the real games -------------------------------------------------------

def game(stem):
    folder = os.path.join(GAMES, stem + ".DSN")
    if not glob.glob(os.path.join(folder, "MON1CHA.DAX")):
        pytest.skip("no %s here" % stem)
    return folder


@pytest.mark.parametrize("stem", ["COKRAW", "DKKRAW"])
def test_real_words(cbin, stem):
    folder = game(stem)
    exe = os.path.join(folder, "START.EXE")
    assert run(cbin, "words", exe) == G.game_words(open(exe, "rb").read())[1:]


@pytest.mark.parametrize("stem", ["COKRAW", "DKKRAW"])
def test_real_monsters_match_reference(cbin, tmp_path, stem):
    folder = game(stem)
    done = 0
    for file_no, path in G.archives(folder, "MON[0-9]CHA.DAX"):
        def blocks(kind):
            p = os.path.join(folder, "MON%d%s.DAX" % (file_no, kind))
            return dict(G.blocks_in_order(p)) if os.path.exists(p) else {}
        wiz, spc, itm = blocks("WIZ"), blocks("SPC"), blocks("ITM")
        for bid, cha in G.blocks_in_order(path):
            want = G.monster(folder, file_no, bid)
            got = c_member(cbin, tmp_path, cha, wiz.get(bid), spc.get(bid, b""),
                           itm.get(bid, b""))
            assert got == want, (file_no, bid)
            done += 1
    assert done > 50


@pytest.mark.parametrize("stem", ["COKRAW", "DKKRAW"])
def test_real_item_table_matches_reference(cbin, tmp_path, stem):
    folder = game(stem)
    args = []
    t0 = G.archives(folder, "ITEM0.DAX")
    if t0:
        blk = G.blocks_in_order(t0[0][1])[0][1]
        ents = b"".join(G.item17(blk[i:i + 17]) for i in range(0, len(blk) - 16, 17))
        args.append("=%s" % write(tmp_path, "item0", ents))
    n = 0
    for pattern in ("ITEM[1-9].DAX", "MON[1-9]ITM.DAX"):
        for _, path in G.archives(folder, pattern):
            for _, blk in G.blocks_in_order(path):
                ents = b"".join(G.item17(r[G.ITEM_TAIL:G.ITEM_TAIL + 17])
                                for r in G.records(blk))
                if ents:
                    args.append(write(tmp_path, "blk%d" % n, ents))
                    n += 1
    out = run(cbin, "table", *args)
    assert [bytes.fromhex(x) for x in out[:out.index("--")]] == G.item_table(folder)


def test_real_map_meets_the_dark_queens_own():
    """The field map is the games' own import code, not a guess: Death
    Knights monsters the Dark Queen also keeps convert to within a few
    bytes of the Dark Queen's record (beyond a stale pointer and the two
    monster-kind bytes, whose encoding the games changed)."""
    import gbmon
    folder = game("DKKRAW")
    moncha = os.path.join(GAMES, "DQKRAW.DSN", "DISK2", "MONCHA.GLB")
    if not os.path.exists(moncha):
        pytest.skip("no Dark Queen here")
    dq = {gbmon.name_of(r): r for r in gbmon.load_library(open(moncha, "rb").read()).values()}
    near = {}
    for bid, _ in G.blocks_in_order(os.path.join(folder, "MON1CHA.DAX")):
        m = G.monster(folder, 1, bid)
        name = gbmon.name_of(m)
        if name in dq:
            a, _ = gbmon.convert(m, None)
            b, _ = gbmon.convert(dq[name], None)
            near[name] = sum(1 for i in range(8, 0x18e)
                             if a[i] != b[i] and i not in (0xbf, 0xc0))
    for name in ("WYNDLASS", "GHAST", "FIRE MINION", "WHISPER SPIDER", "SKEL WARRIOR"):
        assert near[name] <= 2, (name, near[name])


# ---- characters -----------------------------------------------------------

def c_qch(cbin, tmp_path, rec, wiz=None, eff=b"", jnk=b""):
    files = []
    for name, data in (("rec", rec), ("wiz", wiz), ("eff", eff), ("jnk", jnk)):
        files.append(write(tmp_path, name, data) if data else "-")
    out = run(cbin, "qch", *files)
    return out[0] if out[0].startswith("ERR") else bytes.fromhex(out[0])


def ref_qch(rec, wiz=None, eff=b"", jnk=b""):
    if len(rec) == G.COK_SIZE:
        return G.dqk_character(G.cok_to_dkk(rec)[0], G.cok_wiz(rec), eff, jnk)
    return G.dqk_character(rec, wiz, eff, jnk)


def some_wiz(rng):
    w = bytearray(G.WIZ_SIZE)
    spells = sorted(rng.randrange(1, 115) for _ in range(rng.randrange(30)))
    w[G.WIZ_CHAR_MEMORIZED - len(spells):G.WIZ_CHAR_MEMORIZED] = bytes(spells)
    for i in range(G.WIZ_KNOWN_N):
        w[G.WIZ_KNOWN + i] = rng.random() < 0.3
    for i in range(G.WIZ_SLOT, G.WIZ_SIZE):
        w[i] = rng.randrange(8)
    return bytes(w)


@pytest.mark.parametrize("size,cls", [(216, 3), (216, 9), (409, 0), (409, 6), (409, 9)])
def test_synthetic_characters_match_reference(cbin, tmp_path, size, cls):
    rng = random.Random(size * 7 + cls)
    for _ in range(6):
        rec = bytearray(rng.randrange(256) for _ in range(size))
        rec[0] = rng.randrange(16)
        rec[0x21 if size == 216 else 0x5b] = cls
        wiz = some_wiz(rng) if size == 216 and rng.random() < 0.8 else None
        eff = bytes(rng.randrange(256) for _ in range(9 * rng.randrange(4)))
        jnk = b"".join(bytes(42) + bytes(4) + t for t in some_items(rng, rng.randrange(6)))
        assert c_qch(cbin, tmp_path, bytes(rec), wiz, eff, jnk) == \
            ref_qch(bytes(rec), wiz, eff, jnk)


def test_character_spells():
    """The caster block's spells land where the Dark Queen's heroes keep
    them: the list packed and translated, the known spells as bits, the
    cleric's and the mage's slots in their rows."""
    k = bytearray(216)
    w = bytearray(G.WIZ_SIZE)
    w[G.WIZ_CHAR_MEMORIZED - 3:G.WIZ_CHAR_MEMORIZED] = bytes([3, 15, 102])
    for s in (1, 15, 102, 99):          # 99 -> the Dark Queen's 137: no bit
        w[G.WIZ_KNOWN + s - 1] = 1
    w[G.WIZ_SLOT:G.WIZ_SLOT + 3] = b"\x05\x04\x02"
    w[G.WIZ_SLOT + 24:G.WIZ_SLOT + 26] = b"\x04\x02"
    w[G.WIZ_SLOT + 8] = 9                # group 1: not carried
    q = G.dqk_character(bytes(k), bytes(w), b"", b"")
    assert list(q[0xce:0xd2]) == [3, 15, 59, 0]
    bits = int.from_bytes(q[0x157:0x167], "little")
    assert [n + 1 for n in range(128) if bits >> n & 1] == [1, 15, 59]
    assert q[0x167:0x16a] == b"\x05\x04\x02" and q[0x179:0x17b] == b"\x04\x02"
    assert q[0x170] == 0
    assert len(q) == G.DQK_SIZE and q[8] == 0


def test_short_record(cbin, tmp_path):
    assert c_qch(cbin, tmp_path, bytes(100)) == "ERR -1"


def save_characters():
    out = []
    for stem, pat in (("DKKRAW", "*.GAM"), ("COKRAW", "*.SAV")):
        out += sorted(glob.glob(os.path.join(GAMES, stem + ".DSN", "SAVE", pat)))
    if not out:
        pytest.skip("no Champions or Death Knights saves here")
    return out


def test_real_characters_match_reference(cbin, tmp_path):
    import gbchar
    for path in save_characters():
        stem = os.path.splitext(path)[0]

        def side(ext):
            p = stem + ext
            return open(p, "rb").read() if os.path.exists(p) else b""

        rec = open(path, "rb").read()
        if path.upper().endswith(".SAV"):
            got = c_qch(cbin, tmp_path, rec, None, side(".SFX"), side(".STF"))
        else:
            got = c_qch(cbin, tmp_path, rec, side(".WIZ"), side(".EFF"), side(".JNK"))
        want = G.character(path)
        assert got == want, path
        fixed, items, nodes = gbchar.parse_qch(want)      # qch.c's reader takes it
        assert gbchar.name_of(want) == rec[1:1 + rec[0]].decode("latin-1").rstrip("\0")[:15]
        assert len(items) == (len(side(".JNK") or side(".STF")) // 63)


def test_real_characters_facts():
    folder = os.path.join(GAMES, "DKKRAW.DSN", "SAVE")
    if not os.path.exists(os.path.join(folder, "CHRDATA4.GAM")):
        pytest.skip("no Death Knights save here")
    import gbchar
    dryden = G.character(os.path.join(folder, "CHRDATA1.GAM"))
    assert gbchar.name_of(dryden) == "SIR DRYDEN"
    assert gbchar.classes_of(dryden) == [("knight", 7)]       # slot 7 -> 1
    flavius = G.character(os.path.join(folder, "CHRDATA4.GAM"))
    assert gbchar.classes_of(flavius) == [("cleric", 6), ("fighter", 7), ("mage", 6)]
    bits = int.from_bytes(flavius[0x157:0x167], "little")
    assert bits & (1 << 14) and bits & (1 << 59)            # Magic Missile; 103 -> 60
    assert flavius[0x179:0x17c] == b"\x04\x02\x02"          # mage 6: 4/2/2


# ---- a character back to the game (gbdaxmon.py character_back) -----------

def some_hero(rng, tmp_path, cok, cls, name="HERO"):
    """A synthetic hero of either game in tmp_path: (path, {ext: bytes})."""
    size = G.COK_SIZE if cok else G.DKK_SIZE
    rec = bytearray(rng.randrange(256) for _ in range(size))
    rec[0] = rng.randrange(1, 16)
    rec[0x21 if not cok else 0x5b] = cls
    files = {}
    if cok:
        files[".SAV"] = bytes(rec)
        ext_e, ext_j = ".SFX", ".STF"
    else:
        files[".GAM"] = bytes(rec)
        if rng.random() < 0.8:
            files[".WIZ"] = some_wiz(rng)
        ext_e, ext_j = ".EFF", ".JNK"
    files[ext_e] = b"".join(bytes([rng.choice([1, 2, 9, 34, 50])]) + bytes(
        rng.randrange(256) for _ in range(8)) for _ in range(rng.randrange(4)))
    files[ext_j] = b"".join(bytes([5]) + b"Thing" + bytes(36) + bytes(4) + t
                            for t in some_items(rng, rng.randrange(1, 6)))
    files = {e: d for e, d in files.items() if d}
    stem = tmp_path / name
    for e, d in files.items():
        (tmp_path / (name + e)).write_bytes(d)
    return str(stem) + (".SAV" if cok else ".GAM"), files


def write_back(tmp_path, path, out):
    """character_back's files over the hero's (an empty one removed)."""
    stem = os.path.splitext(path)[0]
    for e in (".SAV", ".GAM", ".WIZ", ".EFF", ".JNK", ".SFX", ".STF"):
        if os.path.exists(stem + e) and e not in out:
            os.remove(stem + e)
    for e, d in out.items():
        open(stem + e, "wb").write(d)


@pytest.mark.parametrize("cok,cls", [(False, 3), (False, 7), (True, 0), (True, 6)])
def test_back_unchanged_is_the_game_file(tmp_path, cok, cls):
    rng = random.Random(cok * 100 + cls)
    for i in range(6):
        path, files = some_hero(rng, tmp_path, cok, cls, "H%d" % i)
        assert G.character_back(G.character(path), path) == files


def test_sources_are_one_to_one():
    srcs = [s for s in G.DKK_SOURCES if s is not None]
    assert len(srcs) == len(set(srcs))


@pytest.mark.parametrize("cok", [False, True])
def test_back_carries_every_changed_byte(tmp_path, cok):
    """A byte of the .QCH the game's record carries, changed, comes back
    when the written files are read again; nothing else moves."""
    rng = random.Random(5 + cok)
    covered = set(range(G.DKK_SIZE))
    if cok:
        covered = {d + i for _, d, n in G.COK_TO_DKK for i in range(n)}
        covered -= {p + i for p in G.COK_DKK_CLEAR for i in range(4)}
    places = [d for d, s in enumerate(G.DKK_SOURCES)
              if s is not None and s in covered and d not in (0x5a, 0x5b, 0xc9)]
    for i in range(8):
        path, _ = some_hero(rng, tmp_path, cok, 3, "C%d" % i)
        q = bytearray(G.character(path))
        old = bytes(q)
        for d in rng.sample(places, 25):
            q[d] = (q[d] + 1 + rng.randrange(255)) & 0xff
        write_back(tmp_path, path, G.character_back(bytes(q), path))
        again = G.character(path)
        assert again[:G.DQK_SIZE] == bytes(q[:G.DQK_SIZE]), \
            [hex(k) for k in range(G.DQK_SIZE) if again[k] != q[k]]
        assert again[G.DQK_SIZE:] == old[G.DQK_SIZE:]


def test_back_name(tmp_path):
    rng = random.Random(11)
    path, files = some_hero(rng, tmp_path, False, 3)
    q = bytearray(G.character(path))
    q[0x68:0x78] = b"ARIANNA".ljust(16, b"\0")
    out = G.character_back(bytes(q), path)
    assert out[".GAM"][0] == 7 and out[".GAM"][1:16] == b"ARIANNA" + bytes(8)


def test_back_items_and_effects(tmp_path):
    """Items kept keep their records (the readied byte the new one's), one
    from the game's archives comes whole, a new one is made; effects
    likewise."""
    rng = random.Random(12)
    path, files = some_hero(rng, tmp_path, False, 3)
    q = G.character(path)
    fixed, items, nodes = G._qch_parts(q)
    old = G.records(files[".JNK"])
    src = bytearray(old[0])
    src[0:6] = b"\x05Other"
    src[G.ITEM_TAIL + 1] = 99                       # a different item
    made = bytearray(G.item17(src[G.ITEM_TAIL:]))
    made[1] = 98
    keep = bytearray(items[-1])
    keep[10] ^= 1                                   # readied flips
    new = [bytes(keep), G.item17(bytes(src[G.ITEM_TAIL:])), bytes(made)]
    fx = list(nodes) + [bytes([0x81, 3, 0, 0xff, 0, 0, 0, 0, 0])]
    fixed = bytearray(fixed)
    fixed[8] = fixed[0xc9] = 3
    out = G.character_back(bytes(fixed) + b"".join(new) + b"".join(fx), path,
                           sources=[bytes(src)])
    recs = G.records(out[".JNK"])
    assert recs[0][:G.ITEM_TAIL + 6] == old[-1][:G.ITEM_TAIL + 6]
    assert recs[0][G.ITEM_TAIL + 6] == old[-1][G.ITEM_TAIL + 6] ^ 1
    assert recs[1][:6] == b"\x05Other" and recs[1][0x2a:0x2e] == bytes(4)
    assert recs[2][G.ITEM_TAIL + 1] == 98 and recs[2][0] == 0
    assert out[".GAM"][0x7c] == 3
    eff = out.get(".EFF", b"")
    assert eff[:len(files.get(".EFF", b""))] == files.get(".EFF", b"")
    assert eff[-9] == 2                             # 0x81 is the game's 2


def test_item_name_from_words():
    words = [None] + ["W%d" % i for i in range(1, 126)]
    assert G.item_name(bytes([5, 0, 0x6f % 126, 18]) + bytes(13), words) == b"W18 W111 "
    assert G.item_name(bytes(17), None) == b""


def test_back_spells(tmp_path):
    """Memorized spells go back sorted, the empty ones first, in the
    game's numbers; a newly known spell sets its byte; Champions keeps
    58 at the front of the record."""
    rng = random.Random(13)
    path, files = some_hero(rng, tmp_path, False, 3)
    files[".WIZ"] = bytes(G.WIZ_SIZE)
    open(os.path.splitext(path)[0] + ".WIZ", "wb").write(files[".WIZ"])
    q = bytearray(G.character(path))
    q[0xce:0xd1] = bytes([59, 15, 3])            # 59 is the game's 102
    q[0x157] |= 1 << 2                           # spell 3 known
    out = G.character_back(bytes(q), path)
    w = out[".WIZ"]
    assert w[G.WIZ_CHAR_MEMORIZED - 3:G.WIZ_CHAR_MEMORIZED] == bytes([3, 15, 102])
    assert w[:G.WIZ_CHAR_MEMORIZED - 3] == bytes(G.WIZ_CHAR_MEMORIZED - 3)
    assert w[G.WIZ_KNOWN + 2] == 1

    path, files = some_hero(rng, tmp_path, True, 0, "COK")
    q = bytearray(G.character(path))
    q[0xce:0x118] = bytes(0x4a)
    q[0xce:0xd0] = bytes([15, 3])
    out = G.character_back(bytes(q), path)
    c = out[".SAV"]
    assert c[0x1e:0x58] == bytes(0x3a - 2) + bytes([3, 15])


def test_real_heroes_come_back_whole():
    """The DOSBox saves' heroes, through the hall's .CCH and back: the
    game's files as they were."""
    import gbchar
    paths = (glob.glob(os.path.join(ROOT, "data", "work", "cok-saves", "*.SAV"))
             + glob.glob(os.path.join(ROOT, "data", "work", "dkk-saves", "*.GAM")))
    if not paths:
        pytest.skip("no DOSBox saves here")
    for path in paths:
        q = G.character(path)
        c, _ = gbchar.convert(q, native=True)
        out = G.character_back(gbchar.to_qch(c, base=q), path)
        stem = os.path.splitext(path)[0]
        for e, d in out.items():
            assert open(stem + e, "rb").read() == d, (path, e)
