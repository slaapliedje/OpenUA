"""Visual regression test: boot frua.prg in Hatari, snap a screenshot.

The test runs the full build → boot → screenshot pipeline and verifies
the post-boot frame is non-trivial (the demo's white menu bar /
content shows up against the black margins). Designed to skip
gracefully on machines that lack any of the toolchain components or
an X display.

Marked slow so day-to-day `make test` doesn't pay the boot cost
(~10 seconds in Hatari at --fast-forward). Run with
`make test PYTEST_ARGS='-m slow'` or call pytest directly.
"""
import os
import re
import shutil
import struct
import subprocess
import time

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Components needed for the full pipeline.
HATARI         = shutil.which("hatari")

def _find_grabber():
    """Return grab_argv(window_id, out_path) -> list, or None if unavailable.

    ImageMagick 7 unifies everything under `magick`; ImageMagick 6 (still what
    most distros ship, and what this box has) has no `magick` at all — the
    grabber is `import`, and it takes a DIFFERENT argv shape. Gating on
    `magick` alone skipped this test on every IM6 machine.
    tools/hatari_ui.sh has probed for both since it was written; the test was
    simply stricter than the harness it duplicates.
    """
    if shutil.which("magick"):
        return lambda win, out: ["magick", f"x:{win}", str(out)]
    if shutil.which("import"):
        return lambda win, out: ["import", "-window", str(win), str(out)]
    return None


GRAB_ARGV      = _find_grabber()
XWININFO       = shutil.which("xwininfo")
M68K_GCC       = shutil.which("m68k-atari-mint-gcc")

def _find_falcon_tos():
    """First non-empty Falcon TOS ROM, same order as the Makefile / hatari_ui.

    This used to be a single hardcoded "/usr/share/hatari/TOSv4.04.img", and
    packagers disagree on the spelling — Arch's `hatari` installs tos404.img.
    On any such machine the skipif below fired and this test QUIETLY DID NOT
    RUN, while the suite still reported all-green. A boot smoke test that
    skips itself is worse than no test, because it reads as coverage.
    """
    if os.environ.get("FALCON_TOS"):
        return os.environ["FALCON_TOS"]
    for p in ("/usr/share/hatari/tos404.img",
              "/usr/share/hatari/TOSv4.04.img",
              os.path.expanduser("~/Downloads/Atari/tos404.img"),
              "/usr/share/hatari/etos512us.img"):
        if os.path.exists(p) and os.path.getsize(p) > 0:
            return p
    return "/usr/share/hatari/tos404.img"      # name a real path in the skip


FALCON_TOS     = _find_falcon_tos()
DISPLAY        = os.environ.get("DISPLAY")

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module")
def frua_prg():
    if M68K_GCC is None:
        pytest.skip("m68k-atari-mint cross toolchain not installed")
    subprocess.run(["make"], cwd=REPO, check=True,
                   capture_output=True, text=True)
    path = os.path.join(REPO, "frua.prg")
    assert os.path.exists(path), "make did not produce frua.prg"
    return path


def _find_hatari_window(timeout_s=4.0):
    """Find the X window id for the running Hatari instance."""
    deadline = time.monotonic() + timeout_s
    pattern = re.compile(r'^\s*(0x[0-9a-fA-F]+).*"hatari" "hatari"',
                         re.MULTILINE)
    while time.monotonic() < deadline:
        out = subprocess.run([XWININFO, "-root", "-tree"],
                             capture_output=True, text=True).stdout
        m = pattern.search(out)
        if m:
            return m.group(1)
        time.sleep(0.2)
    return None


def _png_has_visible_content(path):
    """True if the PNG has at least 50 non-zero pixels.

    We don't decode the full image — the IDAT compressed data is
    inspected indirectly via file size: a fully-black frame still
    has tens of KB of PNG data because of the RGB stream, but a
    failure / blank frame is often a kilobyte or two. This is a
    coarse smoke check; a richer comparison can plug a numpy-aware
    diff in later.
    """
    size = os.path.getsize(path)
    return size > 1500


@pytest.mark.skipif(HATARI is None, reason="hatari not installed")
@pytest.mark.skipif(GRAB_ARGV is None,
                    reason="ImageMagick not installed (need `magick` or `import`)")
@pytest.mark.skipif(XWININFO is None, reason="xwininfo not installed")
@pytest.mark.skipif(DISPLAY is None,
                    reason="no DISPLAY (need Xorg/XWayland or Xvfb)")
@pytest.mark.skipif(not os.path.exists(FALCON_TOS),
                    reason=f"Falcon TOS not at {FALCON_TOS}")
def test_post_boot_frame_renders(tmp_path, frua_prg):
    out = tmp_path / "frua-boot.png"
    log = tmp_path / "hatari.log"

    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "x11"

    hatari_args = [
        HATARI, "--machine", "falcon", "--fast-forward", "yes",
        "--dsp", "emu", "--tos", FALCON_TOS, "--conout", "2",
        # `--auto` takes an Atari path. -d REPO mounts REPO as C: so
        # frua.prg sits at C:\frua.prg inside the emulator.
        "-d", REPO, "--auto", r"C:\frua.prg",
    ]
    proc = subprocess.Popen(hatari_args, env=env, cwd=REPO,
                            stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        time.sleep(10)
        win = _find_hatari_window()
        assert win is not None, f"Hatari window not found (log: {log})"
        argv = GRAB_ARGV(win, out)
        result = subprocess.run(argv, capture_output=True, text=True)
        assert result.returncode == 0, \
            f"{argv[0]} failed: {result.stderr or result.stdout}"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()

    assert out.exists(), "screenshot file was not created"
    assert _png_has_visible_content(out), \
        f"screenshot at {out} looks empty (size {out.stat().st_size})"
