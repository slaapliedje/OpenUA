"""The AREA automap's two painters must agree on which global is which axis.

jt221 is the Mac's automap entry. Its callers push (x, y, facing), but jt221
SWAPS them before painting: L52b8's coord1 is `pea %fp@(11)`, and fp@(11) is
argument TWO.  From CODE 7 + 0x608e:

    6092: moveb %fp@(9),%d0   / 6098: movew %d0,%fp@(-2)   ; save arg1 (x)
    609c: moveb %fp@(11),%d0  / 60a2: movew %d0,%fp@(-4)   ; save arg2 (y)
    ...
    60b6: pea %fp@(9)         ; L52b8 coord2  <- x
    60ba: pea %fp@(11)        ; L52b8 coord1  <- y   (last push = param 1)
    60be: jsr %pc@(L52b8)

and the call site, CODE 21 + 0x412e (the AREA toggle L4120):

    4130: moveb %a5@(-12286)  -> push   ; facing (param 3)
    4136: moveb %a5@(-12287)  -> push   ; param 2 = y
    413e: moveb %a5@(-12288)  -> push   ; param 1 = x   (last push)
    4146: jsr %a5@(1802)                ; JT[221]

So the pairing is  axis1/coord1 = -12287  and  axis2/coord2 = -12288.

28b1a2c5 inlined jt221's painter into jt312's map leg (to stop jt221's chrome
prelude leaking three stray FRAME plates over the map) and copied the CALL's
argument order instead of jt221's binding — so the post-move automap repaint
drew the party at the TRANSPOSED cell.  Reported as "the map places you in a
spot, then redraws and you're in another spot".  It is invisible whenever
row == col, which is why a trace taken at (9,9) looked clean; it was caught at
(10,8), where the two legs logged 810 vs 1008.

These are source pins: the pairing lives in 68k semantics no host-side model
can execute, and it has shipped inverted once already (#98), so the point is
to make a silent flip impossible rather than to re-derive it.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOT = (ROOT / "src" / "engine" / "boot.c").read_text()


def _strip_comments(src):
    """Drop /* ... */ and // ... so prose about the bug can't satisfy a pin."""
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def _body(name):
    """Source of the function `name`, comments removed, to its closing brace."""
    m = re.search(r"^static [^\n]*\b%s\s*\([^;]*?\)\s*\n?\{" % re.escape(name),
                  BOOT, re.M)
    assert m, "could not locate %s()" % name
    depth, i = 0, m.end() - 1
    while i < len(BOOT):
        if BOOT[i] == "{":
            depth += 1
        elif BOOT[i] == "}":
            depth -= 1
            if depth == 0:
                return _strip_comments(BOOT[m.start():i + 1])
        i += 1
    raise AssertionError("unterminated %s()" % name)


def test_jt221_binds_coord1_from_its_SECOND_argument():
    """jt221(x, y, facing) -> l52b8(&y, &x): the swap the Mac asm does."""
    body = _body("jt221")
    m = re.search(r"signed char\s+cy\s*=\s*\(signed char\)\s*(\w+)\s*,"
                  r"\s*cx\s*=\s*\(signed char\)\s*(\w+)\s*;", body)
    assert m, "jt221's map branch no longer declares cy/cx from its args"
    assert (m.group(1), m.group(2)) == ("y", "x"), (
        "jt221 must bind cy from its SECOND parameter (y) and cx from the "
        "first (x) — asm 0x60b6/0x60ba pushes fp@(11) as L52b8's coord1, and "
        "fp@(11) is argument two. Got cy=%s, cx=%s." % m.groups())
    # ...and it must then pass them in that order.
    assert re.search(r"l52b8\(\s*&cy\s*,\s*&cx\s*,", body)
    assert re.search(r"l50fe\(\s*\(short\)cy\s*,\s*\(short\)cx\s*,", body)


def test_area_toggle_calls_jt221_in_mac_argument_order():
    """L4120 pushes -12288, -12287, -12286 => jt221(x=-12288, y=-12287)."""
    body = _body("l40f8_area_cmd")
    m = re.search(r"jt221\(\s*\(short\)\(signed char\)g_a5_(\d+)\s*,"
                  r"\s*\(short\)\(signed char\)g_a5_(\d+)\s*,"
                  r"\s*\(short\)\(signed char\)g_a5_(\d+)\s*\)", body)
    assert m, "l40f8_area_cmd no longer calls jt221 with three A5 globals"
    assert m.groups() == ("12288", "12287", "12286"), (
        "AREA toggle must call jt221(-12288, -12287, -12286), matching the "
        "Mac push order at CODE 21 + 0x412e. Got %r." % (m.groups(),))


def test_jt312_map_leg_uses_the_SAME_pairing_as_jt221():
    """The inlined painter must reproduce jt221's binding, not its call order.

    This is the actual regression: cy comes from -12287 (jt221's `y`), NOT
    from -12288 (the first argument its caller pushes).
    """
    m = re.search(r"signed char cy = \(signed char\)g_a5_(\d+);\s*\n"
                  r"\s*signed char cx = \(signed char\)g_a5_(\d+);",
                  _strip_comments(BOOT))
    assert m, "jt312's map leg no longer declares cy/cx from A5 globals"
    assert m.groups() == ("12287", "12288"), (
        "jt312's map leg is TRANSPOSED: it must bind cy from -12287 and cx "
        "from -12288 (jt221's own binding), else the post-move automap draws "
        "the party at the swapped cell. Got cy=g_a5_%s, cx=g_a5_%s."
        % m.groups())


def test_both_automap_painters_agree():
    """Belt and braces: the two legs must not disagree, whatever the values."""
    jt221 = _body("jt221")
    # jt221: cy <- y <- second arg <- -12287 at the call site.
    assert re.search(r"cy\s*=\s*\(signed char\)\s*y\b", jt221)
    leg = re.search(r"signed char cy = \(signed char\)g_a5_(\d+);",
                    _strip_comments(BOOT))
    call = re.search(r"jt221\(\s*\(short\)\(signed char\)g_a5_\d+\s*,"
                     r"\s*\(short\)\(signed char\)g_a5_(\d+)\s*,",
                     _strip_comments(BOOT))
    assert leg and call
    assert leg.group(1) == call.group(1), (
        "the inlined map leg (cy from -%s) and jt221's y argument (-%s) name "
        "different globals — one of the two automap paints is transposed"
        % (leg.group(1), call.group(1)))
