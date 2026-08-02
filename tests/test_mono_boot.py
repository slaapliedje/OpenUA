"""Smoke test: the MONO (BWMODE) build still boots to the main menu.

Mono has silently rotted twice. Both times the diagnosis was expensive and both
times the cause was environmental, not a code regression — most recently it
spent two weeks recorded as "hangs at boot" when it was really failing to open
`ALWAYS.tlb` and sitting on the disk-swap prompt forever (see the `jt987`
bounds in src/engine/boot.c). Nothing in CI or the day-to-day harness compiles
the mono target, let alone boots it, so nobody found out until someone went
looking.

This is the cheapest thing that would have caught it: build the mono binary,
boot it on an emulated ST with a mono monitor, and assert the engine's own
`menu: modal up` marker appears.

★ MONO NEEDS THE MAC B&W `.TLB` ART SET. In mono the engine selects the `.tlb`
art set, and a Mac B&W `.TLB` is a DIFFERENT FORMAT from a DOS HLIB `.TLB`
despite the shared extension — `jt398` rejects the HLIB one by design. Only 17
of the 23 libraries have an install-time mono synthesiser
(`tools/art_convert.py`, `MONO_FAMILIES`); the six chrome ones — ALWAYS, FRAME,
GEN, MENU, TITLE, TOPVIEW — do not, and ALWAYS is the first thing the boot
wants. So this test builds its data tree from the Mac release and SKIPS when
that release is not unpacked. Sanity check if it ever misbehaves: the Mac B&W
`ALWAYS.TLB` is 1816 bytes, the DOS HLIB one 5368.

Marked slow: it pays a mono build plus a boot. Run with `make test-slow`.
"""
import os
import shutil
import subprocess
import tempfile

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(REPO, "data", "work", "gamedata")
MACREL = os.path.join(REPO, "data", "frua-mac")
TOS = "/usr/share/hatari/tos206us.img"
DRIVER = os.path.join(REPO, ".claude", "skills", "run-falcon-port", "driver.sh")

pytestmark = pytest.mark.slow


def _mac_bw_libraries():
    """Every *.TLB in the unpacked Mac release — the authored B&W art set."""
    hits = []
    for root, _dirs, files in os.walk(MACREL):
        for f in files:
            if f.lower().endswith(".tlb"):
                hits.append(os.path.join(root, f))
    return hits


def _requirements():
    """Return a skip reason, or None when the whole pipeline is available."""
    if not shutil.which("m68k-atari-mint-gcc"):
        return "cross toolchain not installed"
    if not os.path.isdir(GAMEDATA):
        return "game data not staged (data/work/gamedata)"
    if not os.path.exists(TOS):
        return "ST TOS 2.06 image not present"
    if not os.path.isfile(DRIVER):
        return "hatari driver missing"
    if len(_mac_bw_libraries()) < 20:
        return ("Mac release not unpacked — mono needs its B&W .TLB art "
                "(the DOS release cannot supply the 6 chrome libraries)")
    return None


def _build_mono_tree(dest):
    """Symlink a mono data tree: staged data, Mac B&W .TLB replacing the art.

    Symlinks so it costs nothing and never mutates data/work/gamedata.
    """
    for name in os.listdir(GAMEDATA):
        if name.lower().endswith((".tlb", ".ctl")):
            continue
        if name.lower() == "frua.prg":
            continue          # the caller links the binary it just BUILT
        os.symlink(os.path.join(GAMEDATA, name), os.path.join(dest, name))
    for name in os.listdir(GAMEDATA):          # keep the colour art as well
        if name.lower().endswith(".ctl"):
            os.symlink(os.path.join(GAMEDATA, name), os.path.join(dest, name))
    for path in _mac_bw_libraries():           # ...and the authored B&W set
        link = os.path.join(dest, os.path.basename(path))
        if not os.path.exists(link):
            os.symlink(path, link)


def test_mono_build_boots_to_the_menu():
    reason = _requirements()
    if reason:
        pytest.skip(reason)

    subprocess.run(
        ["make", "CPU68K=68000", "EXTRA_CFLAGS=-DFRUA_BWMODE"],
        cwd=REPO, check=True, stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT, timeout=1800)

    prg = os.path.join(REPO, "frua.prg")
    assert os.path.isfile(prg), "mono build produced no frua.prg"

    with tempfile.TemporaryDirectory(prefix="frua-mono-") as tree:
        _build_mono_tree(tree)
        os.symlink(prg, os.path.join(tree, "frua.prg"))

        env = dict(os.environ)
        env.pop("DISPLAY", None)               # never land on the user's desktop
        env.update(FALCON_TOS=TOS, FRUA_MEM="4", GEMDOS_DIR=tree,
                   HATARI_ARGS="--machine st --monitor mono --dsp none")
        subprocess.run(["pkill", "-9", "-x", "hatari"], check=False)
        try:
            subprocess.run([DRIVER, "start"], cwd=REPO, env=env, timeout=300,
                           stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        except subprocess.TimeoutExpired:
            pass                               # the log below is the verdict
        finally:
            subprocess.run(["pkill", "-9", "-x", "hatari"], check=False)

        log = os.path.join(tree, "DBG.LOG")
        text = open(log, errors="replace").read() if os.path.exists(log) else ""

        # A jt987 miss is the failure this test exists to catch, and it names
        # the resource — surface it rather than just "no menu marker".
        missed = [l for l in text.splitlines() if "jt987:" in l]
        assert "menu: modal up" in text, (
            "mono did not reach the main menu.\n"
            + ("Unloadable resources:\n  " + "\n  ".join(missed)
               if missed else "No jt987 misses; see the tail below.\n"
               + "\n".join(text.splitlines()[-15:])))
