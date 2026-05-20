"""Build smoke test: `make` produces a valid Atari TOS executable.

Skipped when the m68k-atari-mint cross toolchain is not installed.
"""
import os
import shutil
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


@pytest.mark.skipif(shutil.which("m68k-atari-mint-gcc") is None,
                    reason="m68k-atari-mint cross toolchain not installed")
def test_make_builds_valid_tos_executable():
    subprocess.run(["make"], cwd=REPO, check=True,
                   capture_output=True, text=True)
    prg = os.path.join(REPO, "frua.prg")
    assert os.path.exists(prg)
    with open(prg, "rb") as f:
        magic = f.read(2)
    assert magic == b"\x60\x1a"        # GEMDOS executable header magic
