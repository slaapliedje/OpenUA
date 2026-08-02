"""The `autoload.dat` boot option resumes a save with no keystrokes.

The option skips BOTH menus, which is exactly what makes it worth a test: the
faithful boot path it bypasses is also the one that loads the UI palette, and
the first cut of this feature rendered the dungeon perfectly with every scrap
of HUD text invisible (`port_hud_text_clut` bails on `g_menu_state != 1`, so
the dungeon's clut 129 leaves the text indices grey-on-grey). Nothing crashed
and nothing logged — it just looked like a font bug. A log assertion cannot
see that, so the happy-path case also LOOKS at the frame: it counts the cyan
pixels of the roster's selected-name text, which the grey-on-grey failure
wipes out (measured 1032 with the palette, 140 without — and those 140 are the
shield cursor, not text).

Three cases, one boot each:

  armed + real slot   -> `autoload: resumed`, no `menu: modal up`
  armed + dead slot   -> `autoload: no such slot`, and the FAITHFUL menu boot
  not armed           -> the faithful menu boot, no autoload line at all

The middle case is the one that guards the interesting invariant: ua_main only
skips the menu when the slot actually opens, so a dud configuration must not
strand the player in a menu-less Training Hall.

Marked slow: three emulator boots. Run with `make test-slow`.
"""
import os
import subprocess

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(REPO, "data", "work", "gamedata")
DRIVER = os.path.join(REPO, ".claude", "skills", "run-falcon-port", "driver.sh")
AUTOLOAD = os.path.join(GAMEDATA, "autoload.dat")
DBGLOG = os.path.join(GAMEDATA, "DBG.LOG")

pytestmark = pytest.mark.slow


def _requirements():
    if not os.path.isfile(os.path.join(REPO, "frua.prg")):
        return "frua.prg not built"
    if not os.path.isdir(GAMEDATA):
        return "game data not staged (data/work/gamedata)"
    if not os.path.isfile(DRIVER):
        return "hatari driver missing"
    return None


def _current_design():
    """The design name start.dat points at, e.g. 'KOBOLD.DSN'."""
    path = os.path.join(GAMEDATA, "start.dat")
    if not os.path.exists(path):
        return ""
    return open(path, "rb").read(34).split(b"\0")[0].decode("latin-1")


def _saved_slots():
    """Slot letters that exist for the current design."""
    d = os.path.join(GAMEDATA, _current_design())
    if not os.path.isdir(d):
        d = GAMEDATA
    return sorted(n[6].upper() for n in os.listdir(d)
                  if n.lower().startswith("savgam") and n.lower().endswith(".csv"))


def _env():
    env = dict(os.environ)
    env.pop("DISPLAY", None)               # never land on the user's desktop
    return env


def _boot(seconds=45):
    """Boot, wait, return the log. Never waits on `menu: modal up` — with the
    option armed that marker never comes, which is the whole point."""
    env = _env()
    subprocess.run(["pkill", "-9", "-x", "hatari"], check=False)
    for f in (DBGLOG,):
        if os.path.exists(f):
            os.remove(f)
    try:
        subprocess.run([DRIVER, "start"], cwd=REPO, env=env, timeout=seconds,
                       stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        pass                               # expected when the menus are skipped
    log = open(DBGLOG, errors="replace").read() if os.path.exists(DBGLOG) else ""
    return log


def _stop():
    subprocess.run(["pkill", "-9", "-x", "hatari"], check=False)


def _cyan_pixels(png):
    """Pixels of the roster's cyan selected-name colour (index 11).

    This is the one thing the missing-UI-palette failure destroys while leaving
    everything else looking right: the dungeon clut overwrites the low text
    indices, so names/coords/labels come out grey-on-grey. Crops the Hatari
    status bar (bottom) and title area (top) so only the emulated screen counts.
    Measured on this fixture: 1032 healthy, 140 broken (the shield cursor).
    """
    from PIL import Image

    im = Image.open(png).convert("RGB")
    w, h = im.size
    im = im.crop((0, 40, w, h - 60))
    return sum(1 for r, g, b in im.getdata() if b > 140 and g > 120 and r < 110)


@pytest.fixture
def restore_autoload():
    """Always leave the tree as we found it — an armed autoload.dat would
    silently change what every other harness run boots into."""
    had = os.path.exists(AUTOLOAD)
    prev = open(AUTOLOAD, "rb").read() if had else None
    yield
    _stop()
    if had:
        open(AUTOLOAD, "wb").write(prev)
    elif os.path.exists(AUTOLOAD):
        os.remove(AUTOLOAD)


def test_armed_autoload_resumes_without_keystrokes(restore_autoload, tmp_path):
    reason = _requirements()
    if reason:
        pytest.skip(reason)
    slots = _saved_slots()
    if not slots:
        pytest.skip("no SavGam*.csv staged for the current design")

    open(AUTOLOAD, "wb").write(slots[0].encode())
    log = _boot()

    assert "autoload: resumed" in log, (
        "autoload.dat armed with slot %s but the engine did not resume.\n%s"
        % (slots[0], "\n".join(log.splitlines()[-15:])))
    assert "menu: modal up" not in log, (
        "the menus were supposed to be skipped, but a modal came up")

    pytest.importorskip("PIL", reason="Pillow needed for the frame check")
    shot = str(tmp_path / "autoload.png")
    subprocess.run([DRIVER, "shots", shot], cwd=REPO, env=_env(), timeout=180,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    assert os.path.exists(shot), "no screenshot captured"
    cyan = _cyan_pixels(shot)
    assert cyan > 400, (
        "the play screen came up with no readable HUD text (%d cyan pixels, "
        "healthy is ~1032). That is the missing-UI-palette failure: skipping "
        "the main menu skipped load_menu_ui, so port_hud_text_clut bails and "
        "the roster/coords/labels render grey-on-grey." % cyan)


def test_dead_slot_falls_back_to_the_faithful_menu(restore_autoload):
    reason = _requirements()
    if reason:
        pytest.skip(reason)
    dead = next((c for c in "ABCDEFGHIJ" if c not in _saved_slots()), None)
    if dead is None:
        pytest.skip("every slot A-J exists — no dead slot to point at")

    open(AUTOLOAD, "wb").write(dead.encode())
    log = _boot(seconds=120)

    assert "autoload: no such slot" in log, "the dead slot was not reported"
    assert "menu: modal up" in log, (
        "a dead slot must leave the faithful menu boot alone — otherwise the "
        "player lands in a Training Hall with no menu behind it")


def test_unarmed_boot_is_unchanged(restore_autoload):
    reason = _requirements()
    if reason:
        pytest.skip(reason)
    if os.path.exists(AUTOLOAD):
        os.remove(AUTOLOAD)

    log = _boot(seconds=120)

    assert "menu: modal up" in log, "the default boot no longer reaches the menu"
    assert "autoload:" not in log, "the option logged despite not being armed"
