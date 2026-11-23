"""The Hall's keyboard source (boot.c l0aae): the Mac appends a shape-7
item whose proc is JT[916] (CODE 12 +0x0cd0), the handler Phase 5 hands
the keys no button takes: the arrows move the roster selection, Page
Up/Down page it, J toggles free training. Installed with no proc, the
arrows beeped and the selection never moved (fixed 2026-11-23; seen in
Hatari, DOS moves it). And JT[916] redraws the roster for the NEW
selection: it reloads -27932 after L0848, as CODE 12 +0x0a52 does.
"""
import os
import re

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "src", "engine", "boot.c")
CODE12 = os.path.join(ROOT, "data", "work", "disasm", "CODE_12.s")


def body(name):
    src = open(SRC).read()
    return re.search(r"\nstatic \w+ %s\([^)]*\)\n\{(.*?)\n\}" % name, src, re.S).group(1)


def test_hall_installs_jt916():
    assert "jt452((long)7, (long)(uintptr_t)&jt916, (long)20, (long)0);" in body("l0aae")


def test_jt916_redraws_the_new_selection():
    b = body("jt916")
    assert re.search(r"l0848\(key\);.*?party = \(unsigned char \*\)\(uintptr_t\)"
                     r"g_a5_long\(-27932\);\s*/\*[^*]*\*/\s*l02dc\(", b, re.S)


@pytest.mark.skipif(not os.path.exists(CODE12), reason="no CODE 12 listing")
def test_the_mac_installs_jt916():
    s = open(CODE12).read()
    assert re.search(r"0cd0:.*clrw %sp@-\n.*0cd2:.*movew #20,%sp@-\n"
                     r".*0cd6:.*\(JT\[916\]\)\n.*0cda:.*movew #7,%sp@-\n"
                     r".*0cde:.*\(JT\[452\]\)", s)
    assert re.search(r"0a4c:.*jsr %pc@\(L0848\)\n.*\n.*0a52:.*%a5@\(-27932\)", s)
