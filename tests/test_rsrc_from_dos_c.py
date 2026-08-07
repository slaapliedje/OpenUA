"""The native frua.rsc builder must agree with the PC one, byte for byte.

installer/rsrc_from_dos.c is a second implementation of tools/rsrc_from_dos.py
(ADR-0017) so an ST or Amiga can build frua.rsc from the user's own CKIT.EXE
with no PC in the loop. Two implementations of one file format is exactly the
shape that drifts, and a drift here is not loud: the archive would still load,
the engine would still boot, and the damage would surface much later as wrong
strings in the UI.

Guards, in order of strength:

  1. byte-compare the two builders over the REAL executable (needs data/, so it
     skips in CI — hence guards 2 and 3);
  2. the SHA-256 known answer, which is what makes the builder REFUSE a
     different DOS build rather than emit a plausible archive of wrong text;
  3. the authored strings and the FRSC header, checked against the generated
     header rather than against a copy pasted here.
"""
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CKIT = os.path.join(ROOT, "data", "dos-frua", "CKIT.EXE")
CSRC = os.path.join(ROOT, "installer", "rsrc_from_dos.c")
HDR = os.path.join(ROOT, "installer", "strs_map_dos12.h")
JSON = os.path.join(ROOT, "installer", "strs_map_dos12.json")

DRIVER = r"""
#include <stdio.h>
#include "rsrc_from_dos.h"
int main(int argc, char **argv) {
    char m[512];
    if (argc == 2 && argv[1][0] == 'T')
        return uainst_sha256_selftest() ? 0 : 1;
    int rc = uainst_rsrc_from_dos(argv[1], argv[2], m, sizeof m);
    fprintf(stderr, "%s\n", m);
    return rc;
}
"""


def _gen_header():
    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                 "gen_strs_map_h.py"),
                    "-o", HDR], check=True, capture_output=True)


@pytest.fixture(scope="module")
def cbuild(tmp_path_factory):
    """Compile the installer's builder for the host, or skip if no cc."""
    _gen_header()
    td = tmp_path_factory.mktemp("rsrcc")
    drv = td / "drv.c"
    drv.write_text(DRIVER)
    exe = td / "drv"
    cc = os.environ.get("CC", "cc")
    try:
        subprocess.run([cc, "-std=gnu99", "-O1",
                        "-I" + os.path.join(ROOT, "installer"),
                        "-o", str(exe), str(drv), CSRC],
                       check=True, capture_output=True)
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        pytest.skip(f"host cc unavailable or failed: {exc}")
    return exe


def test_sha256_known_answer(cbuild):
    """FIPS "abc" vector. Data-free, so this one runs in CI.

    A subtly wrong digest would not corrupt anything — it would make the
    installer reject every legitimate CKIT.EXE, which reads to a user as a bad
    disk rather than a bad build. That is worth its own guard.
    """
    assert subprocess.run([str(cbuild), "T"]).returncode == 0


def test_authored_strings_come_from_the_python_tool():
    """The 37 port-authored entries must not be a second hand-typed copy."""
    _gen_header()
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import rsrc_from_dos  # noqa: E402

    text = open(HDR).read()
    body = text.split("sm_authored[SM_NAUTHORED] = {")[1]
    got = dict(re.findall(r'\{\s*(\d+),\s*\d+,\s*"((?:[^"\\]|\\.)*)"\s*\}', body))
    assert len(got) == len(rsrc_from_dos.AUTHORED)
    for off, raw in rsrc_from_dos.AUTHORED.items():
        emitted = got[str(off)].encode().decode("unicode_escape").encode("latin-1")
        assert emitted == raw, f"authored string at {off} drifted"


def test_header_matches_the_json_map():
    """Sizes and the digest are the refusal gate; pin them to the JSON."""
    _gen_header()
    doc = json.load(open(JSON))
    text = open(HDR).read()
    assert f"#define SM_POOL_SIZE  {doc['pool_size']}L" in text
    assert f"#define SM_CKIT_SIZE  {doc['ckit_size']}L" in text
    assert f"#define SM_NENTRIES   {len(doc['entries'])}" in text
    sha = bytes.fromhex(doc["ckit_sha256"])
    emitted = re.search(r"sm_ckit_sha256\[32\] = \{\s*([^}]*)\}", text).group(1)
    assert bytes(int(x) for x in emitted.replace("\n", "").split(",") if x.strip()) == sha


@pytest.mark.skipif(not os.path.isfile(CKIT), reason="needs data/dos-frua/CKIT.EXE")
def test_c_output_is_byte_identical_to_python(cbuild, tmp_path):
    """The whole point: one file format, two implementations, same bytes."""
    c_out, py_out = tmp_path / "c.rsc", tmp_path / "py.rsc"
    assert subprocess.run([str(cbuild), CKIT, str(c_out)]).returncode == 0
    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                 "rsrc_from_dos.py"),
                    CKIT, "-o", str(py_out)], check=True, capture_output=True)
    assert c_out.read_bytes() == py_out.read_bytes()

    # ...and that those bytes really are the FRSC an engine expects.
    blob = c_out.read_bytes()
    magic, ver, count, ent_off, _ = struct.unpack(">4sHHII", blob[:16])
    rtype, rid, attrs, doff, dlen = struct.unpack(">4shHII", blob[16:32])
    assert (magic, ver, count, ent_off) == (b"FRSC", 1, 1, 16)
    assert (rtype, rid, attrs, doff) == (b"STRS", 0, 0, 32)
    assert dlen == json.load(open(JSON))["pool_size"] == len(blob) - 32


@pytest.mark.skipif(not os.path.isfile(CKIT), reason="needs data/dos-frua/CKIT.EXE")
def test_a_different_executable_is_refused(cbuild, tmp_path):
    """It must refuse, not emit a plausible archive of the wrong strings.

    Both gates are exercised: a truncated file (size) and a same-size file with
    one byte changed (digest) — a size-only check would wave the second one
    through and produce an archive that looks fine and reads wrong.
    """
    data = bytearray(open(CKIT, "rb").read())
    short = tmp_path / "short.exe"
    short.write_bytes(data[:-1])
    assert subprocess.run([str(cbuild), str(short), str(tmp_path / "a.rsc")],
                          capture_output=True).returncode != 0
    assert not (tmp_path / "a.rsc").exists()

    data[len(data) // 2] ^= 0xFF
    tweak = tmp_path / "tweak.exe"
    tweak.write_bytes(bytes(data))
    assert len(tweak.read_bytes()) == os.path.getsize(CKIT)   # size gate passes
    assert hashlib.sha256(tweak.read_bytes()).hexdigest() != \
        json.load(open(JSON))["ckit_sha256"]
    assert subprocess.run([str(cbuild), str(tweak), str(tmp_path / "b.rsc")],
                          capture_output=True).returncode != 0
    assert not (tmp_path / "b.rsc").exists()
