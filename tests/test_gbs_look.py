"""Look in a Dark Queen script area runs the square's searchLocation as one
who searches (boot.c gbs_look; DQK.EXE 0x32f51 keeps the search cell, sets
it to 1, runs the block's searchLocation through 0x32c66, puts it back).

Checked in Hatari on a copy of the game's folder with a test ECL004.DAT
whose searchLocation prints only when cell 0x19 is 1 (2026-11-24): Look
printed it and moved the clock ten minutes, the next plain step printed
nothing, and a step with Search on printed it again.
"""
import os
import re

SRC = open(os.path.join(os.path.dirname(__file__), "..", "src", "engine",
                        "boot.c")).read()
BODY = re.search(r"static int gbs_look\(void\)\n\{(.*?)\n\}", SRC, re.S).group(1)


def test_look_runs_search_location_and_puts_the_bit_back():
    run = BODY.index("gbs_run(ECL_ENTRY_SEARCH)")
    back = BODY.index("h[25] = (unsigned char)(h[25] & 1)")
    assert run < back                       # FRUA's Look bit 1 cleared after
    assert "if (!g_gbs_active)\n\t\treturn 0;" in BODY


def test_both_look_arms_ask_the_script_first():
    arms = re.findall(r"if \(!gbs_look\(\)\)[^\n]*\n\s*l709e\(special\);", SRC)
    assert len(arms) == 2                   # jt953's arm and the walk loop's


def test_the_search_cell_reads_both_bits():
    var = re.search(r"case GBV_CELL_SEARCH:(.*?)return 1;", SRC, re.S).group(1)
    assert "[25] & 3) != 0" in var
