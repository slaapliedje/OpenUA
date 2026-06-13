"""Tests for the JT-lift scoreboard classifier (tools/jt_progress.py).

The LIFTED-vs-STUB distinction is the crux of the progress count: a stub
returns a constant and ignores its inputs; a real lift computes from its
args / delegates / reads state. These lock that boundary so the score
can't silently inflate (stubs counted as done) or deflate (tiny-but-real
one-liners counted as stubs).
"""
import jt_progress as jp


def cls(body, n=5000):
    """Classify a synthetic body for an out-of-whitelist JT number."""
    return jp.classify(n, {n: body})


def test_empty_body_is_stub():
    assert cls("\n\tPROBE(\"jt5000\");\n") == "STUB"


def test_void_casts_only_is_stub():
    assert cls("\n\tPROBE(\"x\");\n\t(void)a; (void)b;\n") == "STUB"


def test_return_zero_is_stub():
    assert cls("\n\tPROBE(\"x\");\n\t(void)a;\n\treturn 0;\n") == "STUB"


def test_return_negative_literal_is_stub():
    assert cls("\n\tPROBE(\"x\");\n\treturn -1;\n") == "STUB"


def test_probe_and_return_on_one_line_stub():
    # the line-sharing case that originally misfired
    assert cls(" PROBE(\"x\"); (void)v; return 0; ") == "STUB"


def test_return_expression_is_lifted():
    # abs(): returns an expression of its argument
    assert cls(" PROBE(\"x\"); return (v < 0) ? (short)-v : v; ") == "LIFTED"


def test_getter_is_lifted():
    assert cls("\n\tPROBE(\"x\");\n\treturn g_a5_long(-4582);\n") == "LIFTED"


def test_delegate_is_lifted():
    assert cls("\n\tPROBE(\"x\");\n\treturn l39ae(s);\n") == "LIFTED"


def test_probe_return_expr_one_line_is_lifted():
    assert cls(" PROBE(\"x\"); return (short)(unsigned char)g_a5_byte(-10374); ") \
        == "LIFTED"


def test_real_body_is_lifted():
    body = ("\n\tshort i;\n\tfor (i = 0; i < n; i++)\n"
            "\t\tbuf[i] = 0;\n\treturn i;\n")
    assert cls(body) == "LIFTED"


def test_noop_whitelist():
    n = next(iter(jp.NOOP))
    assert jp.classify(n, {}) == "NOOP"          # done even when absent


def test_alias_lifted():
    n = next(iter(jp.ALIAS_LIFTED))
    assert jp.classify(n, {}) == "ALIAS"


def test_missing_when_absent():
    assert jp.classify(424242, {}) == "MISSING"


def test_standin_number_with_real_body_is_standin():
    # a flagged stand-in has a real body but is NOT a faithful lift
    n = next(iter(jp.STANDIN))
    body = " PROBE(\"x\"); l309c_tile(page, top, left, h, idx); "
    assert jp.classify(n, {n: body}) == "STANDIN"


def test_standin_flag_does_not_mask_a_stub_regression():
    # if a stand-in decays to a bare stub, it must read as STUB, not STANDIN
    n = next(iter(jp.STANDIN))
    assert jp.classify(n, {n: " PROBE(\"x\"); return 0; "}) == "STUB"


def test_standin_absent_body_is_not_standin():
    # no body at all -> MISSING/ALIAS rules win; STANDIN needs a real body
    n = next(iter(jp.STANDIN))
    assert jp.classify(n, {}) in ("MISSING", "ALIAS")
