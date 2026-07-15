"""data_pool_decode.h (the runtime DATA/DREL decoder that lets a STUB build
reconstruct the A5 world from the user's frua.rsc, carrying zero copyrighted
bytes) must be bit-identical to the tools/datapool.py reference the build-time
emitter uses.

Two checks, both through the HOST compiler:
  synthetic — a hand-built compressed DATA + ZERO + DREL (no copyrighted data,
              always runs): C dp_expand == datapool.expand_data, and the C
              DREL word split == datapool._split_reloc_word.
  real      — when frua.rsc is present, the SAME comparison on the actual
              DATA/ZERO/DREL resources (this is what ships): the C decode must
              reproduce dataemit.py's g_a5_init_bytes byte-for-byte.
"""
import os
import shutil
import struct
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))

from datapool import expand_data, parse_drel, RELOC_BASE_A4  # noqa: E402

# A tiny C driver that reads DATA/ZERO/DREL blobs (as files) and dumps the
# expanded image + the decoded reloc stream, so pytest can diff against Python.
HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "data_pool_decode.h"

static unsigned char *slurp(const char *p, long *n)
{
	FILE *f = fopen(p, "rb"); unsigned char *b; long sz;
	if (!f) { perror(p); exit(2); }
	fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc(sz ? sz : 1); fread(b, 1, sz, f); fclose(f); *n = sz; return b;
}

int main(int argc, char **argv)
{
	long dlen, zlen, rlen, L, i;
	unsigned char *data = slurp(argv[1], &dlen);
	unsigned char *zero = slurp(argv[2], &zlen);
	unsigned char *drel = slurp(argv[3], &rlen);
	unsigned char *img;

	L = dp_expand(data, dlen, zero, zlen, NULL);
	if (L < 0) { fprintf(stderr, "expand -1\n"); return 3; }
	img = malloc(L ? L : 1);
	if (dp_expand(data, dlen, zero, zlen, img) != L) { fprintf(stderr, "len mismatch\n"); return 4; }

	/* line 1: expanded length + hex of the image */
	printf("%ld ", L);
	for (i = 0; i < L; i++) printf("%02x", img[i]);
	printf("\n");
	/* line 2: decoded relocs as "base:offset" per word */
	for (i = 0; i + 2 <= rlen; i += 2) {
		unsigned short w = ((unsigned short)drel[i] << 8) | drel[i+1];
		short off; int a4 = dp_drel_word(w, &off);
		printf("%d:%d ", a4, off);
	}
	printf("\n");
	return 0;
}
"""


def _build(tmp_path):
	harness = tmp_path / "h.c"
	harness.write_text(HARNESS)
	exe = tmp_path / "dp_test"
	subprocess.run(
		["cc", "-O2", "-Wall", "-o", str(exe), str(harness),
		 "-I", os.path.join(REPO, "src", "engine")],
		check=True, capture_output=True, text=True)
	return exe


def _run(exe, tmp_path, data, zero, drel):
	(tmp_path / "d").write_bytes(data)
	(tmp_path / "z").write_bytes(zero)
	(tmp_path / "r").write_bytes(drel)
	out = subprocess.run(
		[str(exe), str(tmp_path / "d"), str(tmp_path / "z"), str(tmp_path / "r")],
		check=True, capture_output=True, text=True).stdout.splitlines()
	L, hexs = out[0].split(" ", 1) if " " in out[0] else (out[0], "")
	img = bytes.fromhex(hexs.strip())
	relocs = out[1].split() if len(out) > 1 else []
	return int(L), img, relocs


def _py_relocs(drel):
	return [f"{1 if e.base == RELOC_BASE_A4 else 0}:{e.a5_offset}"
	        for e in parse_drel(drel).entries]


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_decode_synthetic(tmp_path):
	exe = _build(tmp_path)
	# compressed DATA: a literal word, a zero-run escape, another literal
	data = b"\xab\xcd" + b"\x00\x00" + b"\x12\x34"
	zero = struct.pack(">H", 5)          # 5 extra zero bytes after the escape
	# DREL: A5 offset -8 (word 0xFFF8) and A4 offset -6 (0xFFF8|1 = 0xFFF9... )
	drel = struct.pack(">HH", 0xFFF8, 0xFFF9)

	L, img, relocs = _run(exe, tmp_path, data, zero, drel)
	py_img = expand_data(data, zero)
	assert img == py_img, (img.hex(), py_img.hex())
	assert L == len(py_img)
	assert relocs == _py_relocs(drel), (relocs, _py_relocs(drel))


@pytest.mark.skipif(shutil.which("cc") is None, reason="no host C compiler")
def test_decode_real_frua_rsc(tmp_path):
	rsc = os.path.join(REPO, "frua.rsc")
	if not os.path.exists(rsc):
		pytest.skip("frua.rsc not built (no copyrighted data present)")
	blob = open(rsc, "rb").read()

	def lookup(want):
		count, tbl = struct.unpack_from(">HI", blob, 6)
		for i in range(count):
			e = tbl + i * 16
			if blob[e:e + 4] == want and struct.unpack_from(">h", blob, e + 4)[0] == 0:
				_, _, off, ln = struct.unpack_from(">hHII", blob, e + 4)
				return blob[off:off + ln]
		raise KeyError(want)

	data, zero, drel = lookup(b"DATA"), lookup(b"ZERO"), lookup(b"DREL")
	exe = _build(tmp_path)
	L, img, relocs = _run(exe, tmp_path, data, zero, drel)

	py_img = expand_data(data, zero)
	assert img == py_img, "C expand != datapool.expand_data on real DATA"
	assert L == len(py_img) == 31336
	assert relocs == _py_relocs(drel)
