"""The stale-stub-comment guard (tools/stub_audit.py).

boot.c's header comments are the map people navigate the lift by, and a stale
"PROBE stub pending its own lift" over a function that was finished months ago
is worse than no comment at all: three separate sessions have gone hunting for
work that did not exist (l3ac6/l40b4, jt985/l11a2, the whole l076e/l08b4 combat
chain). This test keeps the map honest — lift a stub, and it fails until the
comment says so.
"""
import os

import pytest

import stub_audit

BOOT = os.path.join(os.path.dirname(__file__), '..', 'src', 'engine', 'boot.c')


@pytest.mark.skipif(not os.path.exists(BOOT), reason='boot.c not present')
def test_no_stale_stub_claims():
    """No comment may call a function a stub when its body is a real lift."""
    _, _, stale = stub_audit.audit(BOOT)
    if stale:
        report = '\n'.join(
            '  %s (comment %d-%d) calls %s a stub, but it is lifted: %s'
            % (s['host'], s['comment'][0], s['comment'][1], s['target'], s['claim'][:70])
            for s in stale)
        pytest.fail('%d stale stub comment(s) in boot.c:\n%s\n\n'
                    'Lifting a stub means updating the comment that describes it.'
                    % (len(stale), report))


def _body(src, name):
    lines = stub_audit.load(src)
    for n, _sig, op, cl in stub_audit.parse_funcs(lines):
        if n == name:
            return stub_audit.classify(lines, op, cl)
    return None


def test_classifier_recognises_a_stub(tmp_path):
    f = tmp_path / 'x.c'
    f.write_text('static void l0001(short a, short b)\n{\n'
                 '\tPROBE("l0001");\n\t(void)a; (void)b;\n}\n')
    assert _body(str(f), 'l0001') == 'STUB'


def test_classifier_recognises_a_real_body(tmp_path):
    f = tmp_path / 'x.c'
    f.write_text('static void l0002(short a)\n{\n'
                 '\tPROBE("l0002");\n\tjt42(a);\n}\n')
    assert _body(str(f), 'l0002') == 'REAL'


def test_void_cast_of_a_call_is_not_a_stub(tmp_path):
    """`(void)f(x);` FORWARDS somewhere — it is a real body, not bookkeeping.

    l62e0 is exactly this: a shim onto the fully-lifted l62e0_c8. Reading it as
    a stub would flag its (accurate) comment as stale.
    """
    f = tmp_path / 'x.c'
    f.write_text('static void l62e0(long h)\n{\n\t(void)l62e0_c8(h);\n}\n')
    assert _body(str(f), 'l62e0') == 'REAL'


def test_out_param_default_is_still_a_stub(tmp_path):
    """A stub that writes a safe default (`*out = -1;`) is still a stub."""
    f = tmp_path / 'x.c'
    f.write_text('static void l3792(short y, short *out)\n{\n'
                 '\tPROBE("l3792");\n\t(void)y;\n\t*out = -1;\n}\n')
    assert _body(str(f), 'l3792') == 'STUB'


def test_one_line_definition_is_parsed(tmp_path):
    """`static void jt510(void) { PROBE("jt510"); }` must not be skipped."""
    f = tmp_path / 'x.c'
    f.write_text('static void jt510(void) { PROBE("jt510"); }\n')
    assert _body(str(f), 'jt510') == 'STUB'


def test_historical_note_is_not_a_stale_claim(tmp_path):
    """"was a PROBE stub, now lifted" is a correct note, not a stale claim."""
    f = tmp_path / 'x.c'
    f.write_text('/* l0003 — this WAS a PROBE stub; it is now a full lift. */\n'
                 'static void l0003(short a)\n{\n\tjt42(a);\n}\n')
    _, _, stale = stub_audit.audit(str(f))
    assert stale == []


def test_a_stale_claim_is_caught(tmp_path):
    f = tmp_path / 'x.c'
    f.write_text('/* l0004 — leaf PROBE stub pending its own lift. */\n'
                 'static void l0004(short a)\n{\n\tjt42(a);\n}\n')
    _, _, stale = stub_audit.audit(str(f))
    assert len(stale) == 1
    assert stale[0]['target'] == 'l0004'


def test_pointer_deref_write_is_a_real_body(tmp_path):
    """`*(char *)p = 1;` is WORK, not a comment.

    A body line beginning with '*' looks exactly like a block-comment
    continuation. Treating it as one hid jt321's real body behind a STUB.
    """
    f = tmp_path / 'x.c'
    f.write_text('static void jt321(void)\n{\n\tPROBE("jt321");\n'
                 '\t*(unsigned char *)(uintptr_t)g_a5_long(-11714) = 1;\n}\n')
    assert _body(str(f), 'jt321') == 'REAL'


def test_writing_an_a5_global_is_a_real_body(tmp_path):
    """Setting engine state IS the lift — jt174's whole Mac body is two stores."""
    f = tmp_path / 'x.c'
    f.write_text('static void jt174(void)\n{\n\tPROBE("jt174");\n'
                 '\tg_a5_12912 = 1;\n\tg_a5_12911 = 1;\n}\n')
    assert _body(str(f), 'jt174') == 'REAL'


def test_probe_only_is_a_stub_claim(tmp_path):
    """"stays PROBE-only" means "still a stub" — and it got away for months.

    l1bfe's header said "L1aea ... and JT[138] / JT[139] ... stay PROBE-only"
    over three functions that were ALL fully lifted. The audit ran green the
    whole time because CLAIM_RE only knew the phrase "PROBE stub", so someone
    read the comment and asked for l1aea to be lifted a second time. Every
    wording that means "this is still a stub" has to be caught.
    """
    f = tmp_path / 'x.c'
    f.write_text('/* l0005 — the inner L1aea leaf stays PROBE-only for now. */\n'
                 'static void l0005(short a)\n{\n\tjt42(a);\n}\n\n'
                 'static void l1aea(short a)\n{\n\tjt43(a);\n}\n')
    _, _, stale = stub_audit.audit(str(f))
    targets = {s['target'] for s in stale}
    assert 'l1aea' in targets, stale


def test_probe_only_in_the_past_tense_is_not_a_claim(tmp_path):
    f = tmp_path / 'x.c'
    f.write_text('/* l0006 — this WAS PROBE-only; it is a full lift now. */\n'
                 'static void l0006(short a)\n{\n\tjt42(a);\n}\n')
    _, _, stale = stub_audit.audit(str(f))
    assert stale == []


def test_brace_in_a_char_literal_does_not_end_the_function(tmp_path):
    """`case '{':` is a BRACE CHARACTER, not a block.

    The CODE-8 word-wrap char classes (l2dca/l2d5e) switch on punctuation,
    braces included. Counting those as real braces closed l2dca early and
    desynchronised every function after it — boot.c went from 2127 parsed
    functions to 1557, and 25 stubs silently vanished from the report.
    """
    f = tmp_path / 'x.c'
    f.write_text("static short l2dca(short ch)\n{\n\tPROBE(\"L2dca\");\n"
                 "\tswitch ((unsigned char)ch) {\n"
                 "\tcase '(': case '[': case '{': case '\"':\n"
                 "\t\treturn 1;\n\tdefault:\n\t\treturn 0;\n\t}\n}\n"
                 "\nstatic void after(void)\n{\n\tPROBE(\"after\");\n}\n")
    names = [n for n, _, _, _ in stub_audit.parse_funcs(stub_audit.load(str(f)))]
    assert names == ['l2dca', 'after'], names
    assert _body(str(f), 'l2dca') == 'REAL'
    assert _body(str(f), 'after') == 'STUB'
