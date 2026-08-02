"""Both compass-face lookups must mask the facing into the table's 0..7 range.

`g_a5_27980` is an EIGHT-entry compass ring labelled with the nearest cardinal.
Dumped live from the replayed DATA pool (first byte of each 3-byte entry):

    entry  0    1    2    3    4    5    6    7  |   8
    byte  'N'  'N'  'E'  'S'  'S'  'S'  'W'  'N' |  0x07
    ring   N   NE    E   SE    S   SW    W   NW  | (start of the next array)

So the table is indexed 0..7 -- but `l1908` normalises facing into **1..8**
(`facing <= 0` -> `+8`), so NORTH arrives as 8. An unmasked `facing * 3` then
reads entry 8, which is the first byte of the following array (0x07), not a
letter; the switch falls to `default:` and NO compass face is drawn.

Two functions read this table:

  * `port_draw_compass` (#124) -- masked, correct;
  * `l67ca` -- the chrome redraw, was left faithful to the Mac asm
    (`moveb a5@(-12286),d0 / muluw #3`, no mask) and therefore broken.

`l67ca` is the draw that runs after the force-full recompose which closing the
AREA map arms, so it decides what you are left looking at. Symptom: face north,
open the AREA map, close it -- the compass loses its needle and its letter and
leaves a bare arch. Every other facing is fine, which is why it read as an
AREA-map bug: a facing-2 A/B is byte-identical before and after the toggle, and
only facing 8 reproduces it.

Verified live on the Falcon (HEIRS, 10,8, one left turn to face north): before
the fix the compass region dropped from 132 bright pixels to 36 (just the mouse
cursor) across the toggle; after, the frames are byte-identical.

Source pins -- the table lives in the replayed DATA pool and the indexing is
68k semantics no host-side model can execute.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOT = (ROOT / "src" / "engine" / "boot.c").read_text()


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


CODE = _strip_comments(BOOT)

# Every read of the direction table, however it is spelled.
READS = re.findall(r"g_a5_27980\s*\[([^\]]+)\]", CODE)


def test_the_table_is_read_in_at_least_two_places():
    """Guard the guard: if the reads vanish, the pins below are vacuous."""
    assert len(READS) >= 2, (
        "expected at least two g_a5_27980[] reads (l67ca and "
        "port_draw_compass); found %d" % len(READS))


def test_every_direction_table_read_masks_the_facing():
    """No read may index with a bare facing -- 8 lands outside the 8 entries."""
    unmasked = [r for r in READS if "& 7" not in r and "& 0x7" not in r
                and "% 8" not in r]
    assert not unmasked, (
        "these g_a5_27980[] reads do not mask the facing into 0..7, so NORTH "
        "(facing 8) indexes entry 8 -- the next array's first byte (0x07), not "
        "a letter -- and draws no compass face: %r" % unmasked)


def test_reads_are_scaled_by_the_three_byte_stride():
    """Entries are 3 bytes; a mask without the stride is a different bug."""
    for r in READS:
        assert "* 3" in r or "*3" in r, (
            "g_a5_27980 entries are 3 bytes wide; this read is missing the "
            "stride: %r" % r)


def test_l67ca_still_switches_on_the_four_cardinals():
    """The mask is only correct if the arms it feeds are unchanged."""
    m = re.search(r"static void l67ca\(void\)\s*\{(.*?)\n\}", CODE, re.S)
    assert m, "l67ca is gone or was renamed"
    body = m.group(1)
    for letter, piece in (("'E'", "25"), ("'N'", "22"),
                          ("'S'", "23"), ("'W'", "24")):
        assert re.search(r"case\s+%s\s*:.*?%s" % (re.escape(letter), piece),
                         body, re.S), (
            "l67ca lost the %s -> piece %s compass arm" % (letter, piece))
