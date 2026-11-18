"""tools/modman — the desktop module manager's install logic and catalog.

Synthetic archives only, built here; the fan corpus under data/ (never
committed) is used when present, for the PKZIP 1.x "imploded" members Python
cannot write.
"""
import glob
import hashlib
import io
import os
import sys
import zipfile

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

from modman import catalog, core                              # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")


def game001(title):
    rec = bytearray(388)
    t = title.encode()
    rec[:len(t)] = t
    return bytes(rec)


def zipped(files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


def src(files, name="mod.zip"):
    return core.Source.from_zip(zipped(files), name)


@pytest.fixture
def target(tmp_path):
    (tmp_path / "FRUA.PRG").write_bytes(b"")
    return core.Target(str(tmp_path))


# ---- recognising a module -------------------------------------------------

@pytest.mark.parametrize("layout,design", [
    ("", "MOD"),                          # loose: named after the archive
    ("Curse.dsn/", "CURSE"),              # a design folder
    ("ua/Game40.dsn/", "GAME40"),         # nested one level down
])
def test_the_three_layouts_seen_in_the_wild(layout, design):
    p = core.plan(src({layout + "GAME001.DAT": game001("A TEST"),
                       layout + "GEO005.DAT": b"g"}))
    assert p.design == design and p.folder == design + ".DSN"
    assert p.title == "A TEST"
    assert sorted(p.files) == ["GAME001.DAT", "GEO005.DAT"]


def test_a_saved_web_page_is_called_what_it_is():
    with pytest.raises(core.ModError, match="web page"):
        core.Source.from_zip(b"<!DOCTYPE HTML PUBLIC ...", "bane.zip")


def test_no_design_and_two_designs_are_refused():
    with pytest.raises(core.ModError, match="no design"):
        core.plan(src({"README.TXT": b"x"}))
    with pytest.raises(core.ModError, match="2 designs"):
        core.plan(src({"A.DSN/GAME001.DAT": game001("A"),
                       "B.DSN/GAME001.DAT": game001("B")}))


def test_what_the_engine_never_reads_is_left_out_and_said_so():
    p = core.plan(src({"X.DSN/GAME001.DAT": game001("X"),
                       "X.DSN/RESIZEPC.EXE": b"MZ",
                       "X.DSN/ART.TMP": b"",
                       "X.DSN/html/docs1.html": b"",
                       "X.DSN/a long name.txt": b"",
                       "X.DSN/SAVE/SAVGAMA.CSV": b"s",
                       "readme-outside.txt": b""}))
    assert sorted(p.files) == ["GAME001.DAT", "SAVE/SAVGAMA.CSV"]
    reasons = dict(p.skipped)
    assert "program" in reasons["X.DSN/RESIZEPC.EXE"]
    assert "subfolder" in reasons["X.DSN/html/docs1.html"]
    assert "8.3" in reasons["X.DSN/a long name.txt"]
    assert "outside" in reasons["readme-outside.txt"]


@pytest.mark.parametrize("name,want", [
    ("game001.dat", "GAME001.DAT"),
    ("8x8db3001.ctl", "8X8D3001.CTL"),    # Mac long art names: ADR-0013 spelling
    ("bigpic0244.ctl", "BIGP0244.CTL"),
    ("sprit0052.ctl", "SPRI0052.CTL"),
    ("CPIC1001.CTL", "CPIC1001.CTL"),
    ("a long name.doc", None),
    ("readme.html", None),
])
def test_names_go_on_the_card_as_upper_case_8_3(name, want):
    assert core.dos83(name) == want


def test_two_files_that_collapse_to_one_name_keep_the_first():
    p = core.plan(src({"GAME001.DAT": game001("X"),
                       "8x8db3001.ctl": b"1", "8X8D3001.CTL": b"2"}))
    assert p.files["8X8D3001.CTL"]() == b"1"
    assert any("same 8.3 name" in why for _, why in p.skipped)


# ---- the OpenUA folder ----------------------------------------------------

def test_a_folder_without_the_engine_is_not_a_target(tmp_path):
    assert "no OpenUA engine" in core.Target(str(tmp_path)).check()


def test_install_list_reinstall_and_remove(target):
    p = core.plan(src({"Curse.dsn/GAME001.DAT": game001("THE CURSE"),
                       "Curse.dsn/GEO005.DAT": b"v1",
                       "Curse.dsn/SAVE/HERO.CCH": b"shipped"}))
    path = target.install(p)
    assert sorted(os.listdir(path)) == ["GAME001.DAT", "GEO005.DAT", "SAVE"]
    assert target.designs() == [("CURSE.DSN", "THE CURSE")]

    with open(os.path.join(path, "SAVE", "SAVGAMA.CSV"), "wb") as fh:
        fh.write(b"my game")
    with open(os.path.join(path, "SAVE", "HERO.CCH"), "wb") as fh:
        fh.write(b"my hero")
    with pytest.raises(core.ModError, match="already installed"):
        target.install(p)

    p2 = core.plan(src({"Curse.dsn/GAME001.DAT": game001("THE CURSE"),
                        "Curse.dsn/GEO005.DAT": b"v2",
                        "Curse.dsn/SAVE/HERO.CCH": b"shipped"}))
    target.install(p2, replace=True)
    save = os.path.join(path, "SAVE")
    assert open(os.path.join(path, "GEO005.DAT"), "rb").read() == b"v2"
    assert open(os.path.join(save, "SAVGAMA.CSV"), "rb").read() == b"my game"
    assert open(os.path.join(save, "HERO.CCH"), "rb").read() == b"my hero"
    assert not any(n.endswith(".NEW") for n in os.listdir(target.path))

    target.remove("curse.dsn")
    assert target.designs() == []


def test_a_failed_copy_leaves_nothing_half_installed(target):
    def boom():
        raise OSError("card pulled out")
    p = core.plan(src({"GAME001.DAT": game001("X"), "GEO005.DAT": b"g"}))
    p.files["GEO005.DAT"] = boom
    with pytest.raises(OSError):
        target.install(p)
    assert sorted(os.listdir(target.path)) == ["FRUA.PRG"]


# ---- the catalog ----------------------------------------------------------

CATALOG = """\
# OpenUA module catalog
[curse]
title  = The Curse of the Fire Dragon
author = Someone
url    = https://example.org/m/curse.zip#frag
size   = {size}
sha256 = {sha}
about  = A test entry # with a hash in it

[bare]
url = http://example.org/bare.zip
"""


def test_catalog_parses_and_keeps_text_after_a_hash():
    es = catalog.parse(CATALOG.format(size=10, sha="ab"))
    assert [e.ident for e in es] == ["curse", "bare"]
    assert es[0].url.endswith("#frag") and es[0].about.endswith("hash in it")
    assert es[0].size == 10 and es[1].size is None and es[1].title == "bare"
    with pytest.raises(core.ModError, match="no url"):
        catalog.parse("[x]\ntitle = y\n")
    with pytest.raises(core.ModError, match="line 1"):
        catalog.parse("url = z\n")


class FakeResponse(io.BytesIO):
    def __init__(self, data):
        super().__init__(data)
        self.headers = {"Content-Length": str(len(data))}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def test_fetch_checks_size_sum_and_kind_and_uses_the_cache(tmp_path):
    good = zipped({"GAME001.DAT": game001("X")})
    entry = catalog.parse(CATALOG.format(size=len(good),
                                         sha=hashlib.sha256(good).hexdigest()))[0]
    calls = []

    def opener(req, timeout):
        calls.append(req.full_url)
        return FakeResponse(good)
    path = catalog.fetch(entry, str(tmp_path), opener=opener)
    assert open(path, "rb").read() == good
    catalog.fetch(entry, str(tmp_path), opener=opener)
    assert len(calls) == 1                                    # second time: the cache

    bad = catalog.parse(CATALOG.format(size=len(good), sha="00" * 32))[0]
    bad.ident = "other"
    with pytest.raises(core.ModError, match="checksum"):
        catalog.fetch(bad, str(tmp_path), opener=opener)
    page = catalog.parse("[p]\nurl = http://x/p.zip\n")[0]
    with pytest.raises(core.ModError, match="web page"):
        catalog.fetch(page, str(tmp_path),
                      opener=lambda r, timeout: FakeResponse(b"<html>gone</html>"))


# ---- PKZIP 1.x, on real archives when they are here ----------------------

IMPLODED = [p for p in glob.glob(os.path.join(ROOT, "data", "**", "*.zip"), recursive=True)
            if os.path.getsize(p) < 4_000_000]


def _imploded():
    for p in IMPLODED:
        try:
            z = zipfile.ZipFile(p)
        except zipfile.BadZipFile:
            continue
        if any(i.compress_type == 6 for i in z.infolist()):
            yield p


@pytest.mark.skipif(not list(_imploded()), reason="no imploded archive under data/")
def test_imploded_members_unpack_with_their_checksums():
    for path in _imploded():
        s = core.Source.from_path(path)
        for _, load in s.entries:        # each load checks the member's CRC
            load()
