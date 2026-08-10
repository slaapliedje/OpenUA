"""tools/jt_call_audit.py — the JT-call drop detector.

Synthetic fixtures only: the real disassembly lives under data/ (copyrighted,
git-ignored), so CI has nothing to point the tool at. These build a two-function
CODE segment and a matching boot.c by hand and assert the tool's three load-
bearing behaviours:

  1. it REPORTS a JT call the asm makes and the C body does not (the #137 shape);
  2. it stays QUIET when the C body makes the call, including via an lXXXX
     alias for the same address;
  3. it never counts THINK C runtime (JT[1..8]) as a dropped call — those are
     switch dispatchers and 32-bit mul/div helpers, and treating them as calls
     buried the real findings under 100+ false hits on the first run here.
"""
import os
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(REPO, "tools", "jt_call_audit.py")

JUMPTABLE = """A5 jump table: 4 entries, base A5+0x20

  JT[   3]  A5+0x0038  CODE  1+0x0158
  JT[   7]  A5+0x0058  CODE  1+0x01ec
  JT[ 124]  A5+0x0400  CODE  6+0x3eea
  JT[ 110]  A5+0x0392  CODE  6+0x33ac
"""

# Two functions. L1000 calls JT[124], JT[110], plus runtime JT[3]/JT[7].
# L2000 calls JT[110] only, and is here so the first function's span has a
# hard end (the tool must not read L2000's calls as L1000's).
SEGMENT = """; CODE segment 9 -- synthetic
L1000:
  1000:  4e56ffa8              linkw %fp,#-88
  1004:  4ead0392              jsr %a5@(914)              ; -> CODE 6+0x33ac  (JT[110])
  1008:  4ead0402              jsr %a5@(1026)             ; -> CODE 6+0x3eea  (JT[124])
  100c:  4ead003a              jsr %a5@(58)               ; -> CODE 1+0x158  (JT[3])
  1010:  4ead0058              jsr %a5@(88)               ; -> CODE 1+0x1ec  (JT[7])
  1014:  4e5e                  unlk %fp
  1016:  4e75                  rts
L2000:
  2000:  4e56ffa8              linkw %fp,#-88
  2004:  4ead0392              jsr %a5@(914)              ; -> CODE 6+0x33ac  (JT[110])
  2008:  4e5e                  unlk %fp
  200a:  4e75                  rts
"""


def run(tmp_path, boot_src, *args):
    dis = tmp_path / "disasm"
    dis.mkdir(exist_ok=True)
    (dis / "jumptable.txt").write_text(JUMPTABLE)
    (dis / "CODE_09.s").write_text(SEGMENT)
    boot = tmp_path / "boot.c"
    boot.write_text(boot_src)
    env = dict(os.environ, JT_AUDIT_DISASM=str(dis), JT_AUDIT_BOOT=str(boot))
    p = subprocess.run([sys.executable, TOOL, *args],
                       capture_output=True, text=True, env=env)
    assert p.returncode == 0, p.stderr
    return p.stdout


DROPPED = """
/* L1000 (CODE 9 + 0x1000) — drops the JT[124] palette commit. */
static void l1000(void)
{
\tjt110(&handle, 0, 0, 1, "X");
\tsome_hand_rolled_thing();
}
"""

FAITHFUL = """
/* L1000 (CODE 9 + 0x1000) — makes both calls. */
static void l1000(void)
{
\tjt110(&handle, 0, 0, 1, "X");
\tjt124(handle);
}
"""

# jt124 IS l3eea (CODE 6+0x3eea) — calling the lXXXX name must satisfy JT[124].
ALIASED = """
/* L1000 (CODE 9 + 0x1000) — reaches JT[124] through its lXXXX alias. */
static void l1000(void)
{
\tjt110(&handle, 0, 0, 1, "X");
\tl3eea(&handle);
}
"""


def test_reports_a_dropped_call(tmp_path):
    out = run(tmp_path, DROPPED)
    assert "l1000" in out
    assert "JT[124]" in out.split("NOT in C")[1]


def test_quiet_when_the_call_is_made(tmp_path):
    out = run(tmp_path, FAITHFUL)
    assert "0 with drops" in out
    assert "NOT in C" not in out


def test_lxxxx_alias_counts_as_the_call(tmp_path):
    out = run(tmp_path, ALIASED)
    assert "0 with drops" in out, "l3eea is JT[124]'s own address"


@pytest.mark.parametrize("jtnum", [3, 7])
def test_think_c_runtime_is_never_a_drop(tmp_path, jtnum):
    """JT[3] is the switch dispatcher, JT[7] the signed long divide. A faithful
    lift writes `switch` and `/`, so neither ever appears in the C body — and
    neither may be reported."""
    out = run(tmp_path, DROPPED)
    # only the finding lines matter; the header names what it excludes
    findings = [ln for ln in out.split("\n") if ln.startswith("   ")]
    assert findings, out
    assert not any("JT[%d]" % jtnum in ln for ln in findings), out


def test_span_stops_at_the_next_function(tmp_path):
    """L1000's span must not swallow L2000. Both call JT[110]; if the spans
    leaked, a body dropping it would be reported twice over."""
    out = run(tmp_path, FAITHFUL)
    assert "0 with drops" in out


# A callback is installed by ADDRESS, not called. Requiring a trailing '(' made
# the tool report every callback installation in the engine as a dropped call.
BY_POINTER = """
/* L1000 (CODE 9 + 0x1000) — installs JT[124] as a callback. */
static void l1000(void)
{
\tjt110(&handle, 0, 0, 1, "X");
\tinstall(&handle, (long)(uintptr_t)&jt124);
}
"""

# Doc comments name jtNNN constantly and PROBE() embeds the name in a string.
# If those counted, every check would pass and the tool would find nothing.
ONLY_IN_COMMENT = """
/* L1000 (CODE 9 + 0x1000) — the palette commit via jt124 is deferred here. */
static void l1000(void)
{
\tPROBE("jt124");
\tjt110(&handle, 0, 0, 1, "X");   /* jt124 would go here */
}
"""


def test_callback_installed_by_pointer_counts(tmp_path):
    out = run(tmp_path, BY_POINTER)
    assert "0 with drops" in out, out


def test_name_in_comment_or_string_does_not_count(tmp_path):
    out = run(tmp_path, ONLY_IN_COMMENT)
    assert "JT[124]" in out.split("NOT in C")[1], out
