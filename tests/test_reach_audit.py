"""Tests for tools/reach_audit.py — the reachability audit.

Every case here is a trap that produced a WRONG answer during development
(641, 115, 187, 96, then 0 "uncalled" bodies on the same tree). The tool is
only useful if it stays sensitive to real dead code AND immune to these, so
each one is pinned:

  - a forward declaration is not a call site   (how jt557 hid)
  - a mention in a comment is not a call site  (boot.c prose names jtNNN
                                                constantly)
  - a PROBE("jtN") string is not a call site
  - a call from a NON-static function IS a call site (the 96-entry version
    only scanned `static` definitions, which falsely condemned l07dc/jt919/
    jt931/jt989)
  - a function's OWN signature line is not a call to itself (skipping from
    the opening brace instead of the signature made every function look
    called, and the audit silently reported zero findings)
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tools"))

reach_audit = pytest.importorskip("reach_audit")


def scan_src(tmp_path, body):
    p = tmp_path / "boot.c"
    p.write_text(body)
    return reach_audit.scan(str(p))


def uncalled_names(info):
    return {k for k, v in info.items()
            if not v["shipping"] and not v["harness"] and not v["external"]}


def test_plain_dead_function_is_found(tmp_path):
    info = scan_src(tmp_path, """
static int jt100(void)
{
\treturn 1;
}
static int jt200(void)
{
\treturn jt300();
}
static int jt300(void)
{
\treturn 0;
}
""")
    dead = uncalled_names(info)
    assert "jt100" in dead          # nobody calls it
    assert "jt300" not in dead      # jt200 does


def test_forward_declaration_is_not_a_call(tmp_path):
    """The jt557 trap: a decl looks like a call to a naive regex."""
    info = scan_src(tmp_path, """
static void jt557(void);
static void jt557(void)
{
\treturn;
}
""")
    assert "jt557" in uncalled_names(info)


def test_comment_mention_is_not_a_call(tmp_path):
    """boot.c discusses jtNNN in prose everywhere — that is not a call."""
    info = scan_src(tmp_path, """
/* jt900 is documented here, and even jt900() appears in the prose. */
static int jt900(void)
{
\treturn 0;
}
// another mention of jt900() in a line comment
""")
    assert "jt900" in uncalled_names(info)


def test_probe_string_is_not_a_call(tmp_path):
    info = scan_src(tmp_path, """
static int jt901(void)
{
\tPROBE("jt901");
\treturn 0;
}
""")
    assert "jt901" in uncalled_names(info)


def test_call_from_non_static_function_counts(tmp_path):
    """The 96-entry trap: callers are not always `static`."""
    info = scan_src(tmp_path, """
static int jt902(void)
{
\treturn 0;
}
void engine_boot(void)
{
\tjt902();
}
""")
    assert "jt902" not in uncalled_names(info)


def test_own_signature_line_is_not_a_self_call(tmp_path):
    """The zero-findings trap: `{` on its own line puts the signature
    OUTSIDE the [open..close] body range, so it was counted as a caller."""
    info = scan_src(tmp_path, """
static int jt903(short a)
{
\t(void)a;
\treturn 0;
}
""")
    assert "jt903" in uncalled_names(info)


def test_address_taken_counts_as_reachable(tmp_path):
    """Installed as a callback = reachable, even with no call syntax."""
    info = scan_src(tmp_path, """
static void jt904(void)
{
\treturn;
}
static void install(void)
{
\tregister_handler(jt904);
}
""")
    assert "jt904" not in uncalled_names(info)


def test_harness_only_call_is_separated(tmp_path):
    """Called solely under #ifdef FRUA_* = absent from a default build."""
    info = scan_src(tmp_path, """
static void jt905(void)
{
\treturn;
}
void engine_boot(void)
{
#ifdef FRUA_HALL
\tjt905();
#endif
}
""")
    assert info["jt905"]["shipping"] == []
    assert info["jt905"]["harness"]          # recorded, but not as shipping


def test_recursion_alone_is_not_reachability(tmp_path):
    info = scan_src(tmp_path, """
static int jt906(short n)
{
\treturn n ? jt906(n - 1) : 0;
}
""")
    assert "jt906" in uncalled_names(info)


def test_unused_attribute_is_reported_separately(tmp_path):
    info = scan_src(tmp_path, """
static int jt907(void) __attribute__((unused));
static int jt907(void)
{
\treturn 0;
}
""")
    assert "jt907" in uncalled_names(info)
    assert info["jt907"]["unused_attr"] is True


def test_port_helpers_are_out_of_scope(tmp_path):
    """cg_*/port_* are ours, not transcriptions — not lift gaps."""
    info = scan_src(tmp_path, """
static void cg_helper(void)
{
\treturn;
}
""")
    assert "cg_helper" not in info


def test_slot_regex_covers_every_a5_spelling():
    """A byte-only assignment regex made the party head (-27928, written via
    g_a5_long) look permanently pinned to 0."""
    for spelling in ("g_a5_14433 = 0;",
                     "g_a5_byte(-14433) = 0;",
                     "g_a5_long(-14433) = 0;",
                     "g_a5_word(-14433) = 0;"):
        assert reach_audit.PIN_RE.search(spelling), spelling
        assert reach_audit.ASSIGN_RE.search(spelling), spelling


def test_assign_regex_does_not_match_comparison():
    assert not reach_audit.ASSIGN_RE.search("if (g_a5_14433 == 0)")
    assert reach_audit.GUARD_RE.search("if (g_a5_14433 == 0)")
