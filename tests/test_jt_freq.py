"""Tests for tools/jt_freq.py — static JT callsite frequency."""
import os
import textwrap
from collections import Counter

import pytest

from jt_freq import (
    classify_stubs,
    count_callsites,
    render_markdown,
    render_plain,
    build_freq_table,
)


def _write(p, text):
    with open(p, "w") as f:
        f.write(text)


def test_count_picks_up_jt_annotations(tmp_path):
    """Three .s files with five JT references between them; the
    counter should aggregate across files and ignore other text."""
    _write(tmp_path / "CODE_01.s", textwrap.dedent("""\
        jsr ...  ; -> CODE 1+0x158  (JT[3])
        jsr ...  ; -> CODE 1+0x158  (JT[3])
        jsr ...  ; -> CODE 6+0x4bf6 (JT[103])
        ; unrelated text
        """))
    _write(tmp_path / "CODE_02.s",
           "jsr ; (JT[3])\njsr ; (JT[488])\n")
    _write(tmp_path / "not_code.txt", "(JT[999])")    # not picked up

    counts = count_callsites(str(tmp_path))
    assert counts[3]   == 3
    assert counts[488] == 1
    assert counts[103] == 1
    assert 999 not in counts


def test_count_returns_empty_on_missing_dir(tmp_path):
    assert count_callsites(str(tmp_path / "nope")) == {}


def test_classify_marks_probe_only_stub(tmp_path):
    src = tmp_path / "boot.c"
    _write(src, textwrap.dedent("""\
        static void jt99(short a)    { PROBE("jt99"); (void)a; }
        static int  jt7(short a, long b) {
            PROBE("jt7");
            (void)a;
            (void)b;
            return 0;
        }
        static int jt100(short ch) {
            PROBE("jt100");
            return (ch >= 'A' && ch <= 'Z') ? 1 : 0;
        }
        """))
    classes = classify_stubs(str(src))
    assert classes[99]  == "stub"
    assert classes[7]   == "stub"
    assert classes[100] == "lifted"


def test_classify_handles_multiline_signature(tmp_path):
    """JT entries whose declaration wraps across lines (jt94 / jt97 in
    the real boot.c) still classify correctly — the non-greedy regex
    finds the function name on the next line."""
    src = tmp_path / "boot.c"
    _write(src, textwrap.dedent("""\
        static void jt94(short page, short row, short col, short style,
                         const char *fmt, ...)      { PROBE("jt94"); (void)page;
                                                      (void)row; (void)col;
                                                      (void)style; (void)fmt; }
        """))
    classes = classify_stubs(str(src))
    assert classes[94] == "stub"


def test_build_freq_table_sorts_by_count():
    counts = Counter({3: 5, 384: 10, 488: 7})
    classes = {3: "lifted", 384: "lifted"}
    rows = build_freq_table(counts, classes, limit=3)
    assert [r.jt for r in rows]    == [384, 488, 3]
    assert [r.count for r in rows] == [10, 7, 5]
    assert rows[1].state == "unknown"


def test_render_markdown_includes_header_and_data():
    rows = build_freq_table(Counter({42: 5, 1: 10}), {1: "lifted"}, limit=2)
    md = render_markdown(rows)
    assert "| JT  |" in md
    assert "lifted" in md
    assert "|   1 |        10 | lifted |" in md


def test_render_plain_is_one_line_per_row():
    rows = build_freq_table(Counter({42: 3}), {42: "stub"}, limit=1)
    out = render_plain(rows)
    assert "JT[  42]      3  stub" == out
