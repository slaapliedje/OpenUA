"""The sticky event-text replay is a REPAINT, not a second performance.

After a text event (l4d26) the port replays the box so the final page survives
the wipes that follow — the DOS behaviour #65 restored. But the replay was
built by copying l4d26's draw loop wholesale, so it kept two things that only
belong to the event's FIRST showing:

  * it started at line 0 and re-ran the page-break clears, cycling the box
    through every page again to arrive at the page it already wanted; and
  * it set g_a5_-27981, the per-character pacer, so jt96 called l435a after
    every glyph and busy-waited to the design's authored text speed.

Together those are "it types out the text, but then it does it again" —
reported against the walking-around sign text, and visible on the caravan
entry chain in the Falcon capture (the second pass starts the moment the
BIGPIC goes away).

The first test pins the law: which lines survive a page break. The second is
the source pin for the pacer flag, which no host-side model can observe.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def surviving_page(text_ids, break_bits):
    """Which text slots are still on screen when l4d26 returns.

    Mirrors play_sticky_text_replay's page scan. `text_ids` is the five
    ev[8]/[10]/[12]/[14]/[16] word slots (0 = empty); `break_bits` is ev[4],
    where bit i means "page-break after line i" — the confirm prompt whose
    jt20 clears the box.

    A break clears, so only the lines AFTER the last break are still visible.
    The break on the last non-empty line does NOT count (#65): it fires before
    the event returns, and honouring it left the box empty — the original
    "flashes instead of persisting".
    """
    last = -1
    for i in range(5):
        if text_ids[i]:
            last = i
    if last < 0:
        return []
    first = 0
    for i in range(last - 1, -1, -1):
        if text_ids[i] and (break_bits & (1 << i)):
            first = i + 1
            break
    return [i for i in range(first, last + 1) if text_ids[i]]


def test_only_the_final_page_survives():
    # One line, confirmed: its own break must not clear it (#65).
    assert surviving_page([11, 0, 0, 0, 0], 0b00001) == [0]
    # Two lines, break between them: only the second survives. The old loop
    # redrew BOTH, which is the visible "cycles through all the text again".
    assert surviving_page([11, 22, 0, 0, 0], 0b00001) == [1]
    # ...and the trailing break on line 1 changes nothing.
    assert surviving_page([11, 22, 0, 0, 0], 0b00011) == [1]
    # No breaks: every line shares one page and all of it stays.
    assert surviving_page([11, 22, 33, 0, 0], 0) == [0, 1, 2]
    # Break at line 0 only: lines 1 and 2 are one page together.
    assert surviving_page([11, 22, 33, 0, 0], 0b00001) == [1, 2]
    # The LAST break wins, not the first.
    assert surviving_page([11, 22, 33, 0, 0], 0b00011) == [2]
    # A break bit on an EMPTY slot never fired, so it cannot have cleared.
    assert surviving_page([11, 0, 33, 0, 0], 0b00010) == [0, 2]
    # Nothing authored.
    assert surviving_page([0, 0, 0, 0, 0], 0b11111) == []


def _replay_body():
    """The definition's body, brace-matched.

    Matched from the definition's opening brace, NOT by "signature to the next
    brace": boot.c carries a forward declaration of this function too, and the
    loose form happily matches the prototype plus whatever function follows it
    (the trap test_glib_t7 records).
    """
    src = (ROOT / "src/engine/boot.c").read_text()
    m = re.search(r"static void play_sticky_text_replay\(void\)\s*\n\{", src)
    assert m, "play_sticky_text_replay definition not found"
    i = src.index("{", m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    raise AssertionError("unbalanced braces in play_sticky_text_replay")


def test_replay_never_enables_slow_text_pacing():
    """g_a5_-27981 = 1 makes jt96 busy-wait per glyph — a re-type, not a repaint.

    l4d26's own loop still sets it (the event's first showing SHOULD honour the
    design's text speed); this function must not. It is a source pin because
    the pacing is a timing effect inside the 68k build — the pixels come out
    the same either way, so no screenshot comparison can catch a regression.
    """
    body = _replay_body()
    assert not re.search(r"g_a5_byte\(-27981\)\s*=\s*1", body), (
        "play_sticky_text_replay must not set the slow-text pacer: it makes "
        "the repaint re-type the text at the design's authored speed"
    )
    assert re.search(r"g_a5_byte\(-27981\)\s*=\s*0", body), (
        "clear the pacer explicitly — the replay must not inherit whatever "
        "state a caller happened to leave it in"
    )


def test_replay_does_not_clear_between_pages():
    """jt20 inside the replay = the box visibly cycling through old pages.

    The replay starts on the surviving page, so there is nothing before it to
    clear. A jt20 here would reintroduce the flash.
    """
    assert "jt20(" not in _replay_body(), (
        "the replay draws the final page only; a page-break clear means it is "
        "walking the earlier pages again"
    )
