"""stage_dos — the one-command end-user DOS staging pipeline must produce a
complete, correct game folder. The per-tool tests already pin each conversion
byte-exactly; this drives the whole orchestration the way a release user
would and checks the assembled results. Skips when the DOS corpus isn't
staged under data/."""
import os
import struct
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOS_ROOT = os.path.join(REPO, "data", "dos-frua")

pytestmark = pytest.mark.skipif(
    not os.path.isdir(DOS_ROOT),
    reason="DOS corpus not staged under data/",
)


def test_stage_dos_builds_a_complete_folder(tmp_path):
	dest = tmp_path / "OPENUA"
	r = subprocess.run(
		[sys.executable, os.path.join(REPO, "tools", "stage_dos.py"),
		 DOS_ROOT, str(dest), "--design", "HEIRS.DSN"],
		capture_output=True, text=True)
	assert r.returncode == 0, r.stderr

	# The essentials exist and carry the right signatures.
	rsc = (dest / "frua.rsc").read_bytes()
	assert rsc[:4] == b"FRSC"
	for bank in ("GAME", "GEO", "MONST", "SCRIPT", "STRG"):
		b = (dest / ("%s.GLB" % bank)).read_bytes()
		assert b[:4] == b"GLIB" and b[12:16] == b"DATA", bank
	snd = (dest / "SOUNDS.GLB").read_bytes()
	assert snd[:4] == b"GLIB" and snd[12:16] == b"DIG8"
	mus = (dest / "MUSIC.SLB").read_bytes()
	assert struct.unpack(">H", mus[8:10])[0] >= 8   # slot count
	# start.dat: 34-byte padded design name + the valid flag.
	sd = (dest / "start.dat").read_bytes()
	assert len(sd) == 35 and sd[34] == 1
	assert sd[:9] == b"HEIRS.DSN"
	# At least one design landed with converted art twins.
	heirs = dest / "HEIRS.DSN"
	assert heirs.is_dir()
	assert any(f.lower().endswith(".ctl") for f in os.listdir(dest))

	# Idempotence: a re-run must not delete user files.
	marker = heirs / "SavGamZ.csv"
	marker.write_bytes(b"user save")
	r2 = subprocess.run(
		[sys.executable, os.path.join(REPO, "tools", "stage_dos.py"),
		 DOS_ROOT, str(dest)],
		capture_output=True, text=True)
	assert r2.returncode == 0, r2.stderr
	assert marker.read_bytes() == b"user save"
