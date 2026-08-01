"""A bare letter in the walk loop must never reach L2d3e's coordinate hit-test.

The Toolbox event mask exists to stop keys being returned as events: every
engine call site passes kind = 7 (null + mouseDown + mouseUp), so on the Mac
jt1125 NEVER returns key events. An earlier lift ignored `kind`, and L2d3e fed
the returned (ascii, modifiers) straight into its (mouse_y, mouse_x) hit-test --
with the pointer over the combat field, every keypress committed the movement
strip under the cursor (the phantom "ATTACK ALLY?" prompt).

Honouring the mask fixed that, but the dungeon walk needs an exception: l63c0's
movement routing lives in L2d3e Phase 1, which is gated on the key being
returned as an event, so with the mask on the party could not move at all.

The exception was written as a blanket `!g_walk_input` -- which handed EVERY
key, letters included, back to the hit-test and reopened the original bug
inside the walk loop. Traced live on the Falcon:

    WALK key kc 13   x8      <- Return: short-circuited, no pad dispatch
    WALK key kc 97           <- 'a'
    PAD dispatch pollres 3   <- matched the movement pad's LEFT edge
    l1908 moved 1            <- jt297(258 + (3-1)*2) = 262 = turn left
    === AREA toggle          <- and THEN the accelerator fired

So one 'a' turned the party and opened the map -- reported as "A turns me left
then goes to the map", with w/s/d flipping the view the same way. It is
modifier-dependent (modifiers become mouse_x), so it misfires intermittently,
which is why it read as a sync/rendering fault rather than an input one.

The exception must therefore be NARROW: only the keys l63c0's keyboard arm
handles -- the movement band 257..264, Esc (27) and Return (13), exactly the
set the Phase 1 short-circuit already routes. Letters still reach the command
bar through the -818/-820 pending stamp, which is the accelerator path and does
not go through the hit-test.

Verified live: with the gate narrowed, the same drive produces no PAD dispatch,
no spurious turn, the AREA toggle still fires, and the first-person view before
and after the toggle is BYTE-IDENTICAL (it differed before the fix).

Source pins -- the behaviour is 68k event plumbing no host-side model can run.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOT = (ROOT / "src" / "engine" / "boot.c").read_text()


def _strip_comments(src):
    """Drop comments so prose about the bug cannot satisfy a pin."""
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


CODE = _strip_comments(BOOT)


def test_walk_exception_is_not_a_blanket_g_walk_input():
    """The mask bypass must not be `(kind & (8|32)) == 0 && !g_walk_input`."""
    blanket = re.search(
        r"if\s*\(\s*\(\s*kind\s*&\s*\(\s*8\s*\|\s*32\s*\)\s*\)\s*==\s*0"
        r"\s*&&\s*!\s*g_walk_input\s*\)", CODE)
    assert blanket is None, (
        "the walk-loop exception is a blanket !g_walk_input again: every key, "
        "letters included, is returned as an event and L2d3e feeds "
        "(ascii, modifiers) to its (mouse_y, mouse_x) hit-test -- a bare 'a' "
        "lands on the movement pad and turns the party before the AREA "
        "accelerator fires.")


def test_walk_exception_admits_only_movement_esc_and_return():
    """Keys returned as events in the walk loop: 257..264, 27, 13 -- no more."""
    m = re.search(
        r"if\s*\(\s*\(\s*kind\s*&\s*\(\s*8\s*\|\s*32\s*\)\s*\)\s*==\s*0"
        r"(?P<cond>.*?)\)\s*\{\s*\*out1\s*=\s*0\s*;", CODE, re.S)
    assert m, "the kind-mask bypass is gone entirely -- keys now always return"
    cond = m.group("cond")
    assert "g_walk_input" in cond, "the walk exception vanished; movement breaks"
    assert re.search(r"ascii\s*>=\s*257", cond) and \
           re.search(r"ascii\s*<=\s*264", cond), (
        "the walk exception no longer restricts to the 257..264 movement "
        "band: %r" % cond)
    assert re.search(r"ascii\s*==\s*27", cond), "Esc dropped from the walk band"
    assert re.search(r"ascii\s*==\s*13", cond), "Return dropped from the walk band"


def test_phase1_shortcircuit_covers_the_same_set():
    """L2d3e Phase 1 routes exactly the keys the mask now lets through.

    If these two sets drift apart, a key is either returned as an event but not
    routed (falls into the hit-test) or routed but never delivered.
    """
    m = re.search(
        r"if\s*\(\s*g_walk_input\s*&&\s*\(\s*\(\s*kc\s*>=\s*257\s*&&"
        r"\s*kc\s*<=\s*264\s*\)\s*\|\|\s*kc\s*==\s*27\s*\|\|\s*kc\s*==\s*13\s*\)",
        CODE)
    assert m, (
        "Phase 1's walk short-circuit no longer matches {257..264, 27, 13}; it "
        "must stay in step with the kind-mask exception or a key falls through "
        "to the coordinate hit-test")
