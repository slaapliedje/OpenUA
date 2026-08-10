#!/usr/bin/env python3
"""jt_call_audit.py — find lifted C functions that DROPPED a JT call the Mac makes.

The #137 black-sprite bug was 65 hand-written lines standing in for one
`jsr JT[124]`. Nothing flagged it: the code compiled, ran, and looked
deliberate (it even carried a comment explaining why the JT call was skipped).
The workaround was correct when written and silently wrong nine commits later,
when the loader it assumed changed underneath it.

This is the structural check that would have caught it. For every function in
src/engine/boot.c annotated with its origin — `/* L09dc (CODE 17 + 0x9dc) */`
or `/* JT[110] (CODE 6 + 0x33ac) */` — it extracts the matching body from
data/work/disasm/CODE_NN.s, collects the JT[n] targets the asm calls, and
reports the ones the C body never calls.

A hit is NOT automatically a bug. Level-1/2 lifts (see CLAUDE.md) legitimately
defer calls, and the report says so. What a hit means is: "the Mac calls this
here and we do not" — which is exactly the question worth asking of any block
that looks hand-rolled.

Usage:
    python3 tools/jt_call_audit.py                     # ranked report
    python3 tools/jt_call_audit.py --func l09dc        # one function
    python3 tools/jt_call_audit.py --min-missing 2     # only multi-drop sites
"""
import argparse
import os
import re
import sys
from collections import OrderedDict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISASM = os.environ.get("JT_AUDIT_DISASM",
                        os.path.join(REPO, "data", "work", "disasm"))
BOOT = os.environ.get("JT_AUDIT_BOOT",
                      os.path.join(REPO, "src", "engine", "boot.c"))

# CODE 1's low jump-table entries are THINK C RUNTIME, not engine calls: a
# faithful C lift never calls them, so counting them as drops buries the real
# findings (they accounted for 100+ of the first 174 hits here).
#
#   JT[1] sparse switch on a word   — pops the return address, walks inline
#   JT[2] sparse switch on a long     (value, offset) pairs, jumps
#   JT[3] range switch              — the min/max/default table in CLAUDE.md
#   JT[4] 32x32 -> 32 multiply      — the 68000 has no muls.l, so the compiler
#   JT[5] unsigned long divide        calls out; in C these are * / %
#   JT[6] unsigned long modulo
#   JT[7] signed long divide
#   JT[8] signed long modulo
#
# Verified by disassembling CODE 1 (0x130..0x20c) and against boot.c's own
# lifts of jt4..jt8, which are literally `return a * b;` / `return a / b;`.
IGNORED_JT = {1, 2, 3, 4, 5, 6, 7, 8}

# `/* L09dc (CODE 17 + 0x9dc)` or `/* JT[418] (CODE 3+0x32e2)`
HDR_RE = re.compile(
    r"^\s*/?\*+\s*(?:(L[0-9a-fA-F]{3,5})|JT\[\s*(\d+)\s*\])\s*"
    r"\(CODE\s*(\d+)\s*\+\s*0x([0-9a-fA-F]+)\)")
# a C function definition line: `static void l09dc(void)` / `static void jt124(long h)`
# Also matches the port's SUFFIXED helpers (l0980_slots, l36e0_c10): a lift is
# often split into `<name>` plus `<name>_something`, and the helper must be
# parseable so its calls can be credited to the parent (see c_called_names).
CDEF_RE = re.compile(
    r"^static\s+[^;{}]*?\b((?:l[0-9a-f]{3,5}|jt\d+)(?:_[A-Za-z0-9_]+)?)\s*\(")
ASM_LABEL_RE = re.compile(r"^L([0-9a-fA-F]+):")
ASM_INSN_RE = re.compile(r"^\s{2}([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+(.*)$")
ASM_JT_RE = re.compile(r"\(JT\[\s*(\d+)\s*\]\)")
JTNUM_RE = re.compile(r"JT\[\s*(\d+)\s*\]\s+A5\+0x[0-9a-fA-F]+\s+CODE\s+(\d+)\+0x([0-9a-fA-F]+)")


def load_jumptable():
    """JT number -> (code segment, offset)."""
    path = os.path.join(DISASM, "jumptable.txt")
    out = {}
    with open(path) as fh:
        for line in fh:
            m = JTNUM_RE.search(line)
            if m:
                out[int(m.group(1))] = (int(m.group(2)), int(m.group(3), 16))
    return out


def load_segment(code):
    """CODE_NN.s -> ordered {offset: (text, is_label_here)} plus label offsets."""
    path = os.path.join(DISASM, "CODE_%02d.s" % code)
    if not os.path.exists(path):
        return None
    insns = []          # (offset, text)
    labels = set()
    with open(path) as fh:
        for line in fh:
            m = ASM_LABEL_RE.match(line)
            if m:
                labels.add(int(m.group(1), 16))
                continue
            m = ASM_INSN_RE.match(line)
            if m:
                insns.append((int(m.group(1), 16), m.group(2)))
    insns.sort()
    return insns, labels


def asm_body(insns, start, hard_end):
    """Instructions of the function entered at `start`.

    THINK C gives a framed function one epilogue: early returns branch to it.
    So the body ends at the first `unlk` + `rts`. A frameless leaf ends at its
    first `rts`. `hard_end` (the next known function entry) is a backstop for
    anything that matches neither — better to under-read than to swallow the
    next function's calls and report them as ours.
    """
    body = []
    seen_link = False
    for off, text in insns:
        if off < start:
            continue
        if off >= hard_end:
            break
        body.append((off, text))
        if text.startswith("link"):
            seen_link = True
        elif text.startswith("unlk"):
            seen_link = "epilogue"
        elif text.startswith("rts"):
            if seen_link in (False, "epilogue"):
                break
    return body


def asm_jt_calls(body):
    """Multiset of JT numbers this asm body calls."""
    calls = {}
    for _off, text in body:
        m = ASM_JT_RE.search(text)
        if m:
            n = int(m.group(1))
            if n not in IGNORED_JT:
                calls[n] = calls.get(n, 0) + 1
    return calls


def parse_c_functions():
    """[(name, code, offset, lineno, body_text)] for annotated boot.c functions."""
    with open(BOOT, encoding="utf-8") as fh:
        lines = fh.read().split("\n")

    funcs = []
    pending = None          # (code, offset, header_line)
    for i, line in enumerate(lines):
        m = HDR_RE.match(line)
        if m:
            pending = (int(m.group(3)), int(m.group(4), 16), i + 1)
            continue
        m = CDEF_RE.match(line)
        if not m:
            continue
        if pending is None:
            continue
        name = m.group(1)
        # Forward declaration, not a definition. Testing only THIS line for a
        # trailing ';' misses the multi-line form, and then brace-matching runs
        # on into the NEXT function and reports its calls under this name. That
        # produced a confident false positive on l33ac (whose real body is 49k
        # lines further down and is perfectly faithful), so decide it properly:
        # whichever of ';' or '{' comes first wins.
        decided = None
        for j in range(i, min(i + 12, len(lines))):
            for ch in lines[j]:
                if ch in ";{":
                    decided = ch
                    break
            if decided:
                break
        if decided != "{":
            continue
        body, depth, started = [], 0, False
        for j in range(i, len(lines)):
            body.append(lines[j])
            depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]:
                started = True
            if started and depth <= 0:
                break
        funcs.append((name, pending[0], pending[1], i + 1, "\n".join(body)))
        pending = None
    return funcs


def parse_all_bodies():
    """name -> body for EVERY static definition, annotated or not.

    The delegation unwrap needs this: a wrapper's callee is often an
    un-annotated helper (jt110 forwards to l33ac, whose real definition carries
    no `(CODE ...)` header), so a map built from annotated functions alone
    silently fails to unwrap and the wrapper reports the whole body as dropped.
    """
    with open(BOOT, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    out = {}
    for i, line in enumerate(lines):
        m = CDEF_RE.match(line)
        if not m:
            continue
        decided = None
        for j in range(i, min(i + 12, len(lines))):
            for ch in lines[j]:
                if ch in ";{":
                    decided = ch
                    break
            if decided:
                break
        if decided != "{":
            continue
        body, depth, started = [], 0, False
        for j in range(i, len(lines)):
            body.append(lines[j])
            depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]:
                started = True
            if started and depth <= 0:
                break
        out.setdefault(m.group(1), "\n".join(body))
    return out


_COMMENT_OR_STRING = re.compile(
    r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", re.S)


def c_called_names(body):
    """Every lifted function this body REFERENCES.

    Not just `name(` — a callback is installed by ADDRESS, e.g.
    `l63c0(rec, 1, 1, 0, (long)&jt237, (long)&jt236)`, and requiring a trailing
    '(' misses those entirely. That made the tool flag every callback
    installation in the engine as a dropped call: jt241, jt240, jt169, l1bfe,
    jt511, l2558 and others were all reported for calls they make perfectly
    well, just by pointer.

    So match bare identifiers instead — but strip comments and string literals
    first, or the doc comments (which name jtNNN constantly) and PROBE("jtNNN")
    would satisfy every check and the tool would find nothing at all.
    """
    code = _COMMENT_OR_STRING.sub(" ", body)
    return set(re.findall(
        r"\b((?:jt\d+|l[0-9a-f]{3,5})(?:_[A-Za-z0-9_]+)?)\b", code))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--func", help="only this C function name")
    ap.add_argument("--min-missing", type=int, default=1)
    ap.add_argument("--max-missing", type=int, default=10 ** 6,
                    help="drop sites above this many missing calls — a body that "
                         "skips dozens of JT calls is an undone lift, not a "
                         "hand-rolled substitution")
    ap.add_argument("--min-clines", type=int, default=0,
                    help="only C bodies at least this long: a PROBE stub cannot "
                         "have REPLACED anything")
    ap.add_argument("--skip-todo", action="store_true",
                    help="drop bodies that admit deferral (TODO / skeleton / "
                         "stub / deferred) — those are declared, not hidden")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(DISASM):
        print("no disassembly at %s — run tools/dis68k.py first" % DISASM)
        return 2

    jt = load_jumptable()
    jt_by_addr = {}
    for n, (code, off) in jt.items():
        jt_by_addr.setdefault((code, off), []).append(n)

    funcs = parse_c_functions()
    if args.func:
        funcs = [f for f in funcs if f[0] == args.func]

    # name -> body, for the one-level delegation unwrap below.
    bodies_by_name = parse_all_bodies()

    segs = {}
    entries = {}            # code -> sorted known function entry offsets
    for _n, code, off, _ln, _b in funcs:
        entries.setdefault(code, set()).add(off)
    for code, off in jt.values():
        entries.setdefault(code, set()).add(off)
    for code in entries:
        entries[code] = sorted(entries[code])

    findings = []
    skipped = 0
    for name, code, off, lineno, body in funcs:
        if code not in segs:
            segs[code] = load_segment(code)
        if segs[code] is None:
            skipped += 1
            continue
        insns, _labels = segs[code]
        ents = entries[code]
        nxt = None
        for e in ents:
            if e > off:
                nxt = e
                break
        hard_end = nxt if nxt is not None else 1 << 30
        abody = asm_body(insns, off, hard_end)
        if not abody:
            skipped += 1
            continue
        want = asm_jt_calls(abody)
        if not want:
            continue
        clen = body.count("\n") + 1
        have = c_called_names(body)
        # Follow ONE level of delegation. The port often splits a single Mac
        # function into a thin wrapper plus the real body (jt110 is an 8-line
        # forwarder to l33ac, and both are CODE 6+0x33ac). Comparing the whole
        # asm against just the wrapper reports every call the body makes as
        # dropped. Only unwrap SMALL bodies with a single lifted callee, so a
        # genuine short function that really does drop a call still reports.
        # `have` includes the definition line, so it always contains the
        # function's own name — exclude it when counting callees.
        others = have - {name}
        if len(others) == 1 and clen <= 15:
            callee = next(iter(others))
            if callee in bodies_by_name:
                have = have | c_called_names(bodies_by_name[callee])
        # Credit DERIVED helpers. The port routinely splits one Mac function
        # into `<name>` plus `<name>_suffix` (l0980 + l0980_slots), and the
        # suffixed half is where the dropped call usually lives — l0980 was
        # reported for JT[661] and JT[670], both of which l0980_slots makes.
        # Only names derived from THIS function are followed, so this cannot
        # launder an unrelated function's calls into the parent.
        for other in list(have):
            if other.startswith(name + "_") and other in bodies_by_name:
                have = have | c_called_names(bodies_by_name[other])
        # Credit helpers named after an asm label INSIDE this function's span.
        # The port also extracts an inner block and names it after its label:
        # jt955's case-2/3 arm is `l45f0_menu`, and L45f0 is an address within
        # jt955 itself. Without this, jt955 reports the four jt155 calls and
        # the jt182 "Blocked:" prompt that l45f0_menu makes perfectly well.
        # Restricted to labels in [start, end) so it cannot credit an unrelated
        # function that merely shares a name shape.
        span_lo, span_hi = abody[0][0], abody[-1][0]
        for other in list(have):
            m2 = re.match(r"^l([0-9a-f]{3,5})(?:_|$)", other)
            if not m2 or other not in bodies_by_name:
                continue
            if span_lo <= int(m2.group(1), 16) <= span_hi:
                have = have | c_called_names(bodies_by_name[other])
        missing = []
        for n in sorted(want):
            tgt = jt.get(n)
            # A JT entry may be lifted under a SUFFIXED name (JT[1154] is
            # jt1154_pg here), so accept `jtN` or `jtN_*`; likewise the lXXXX
            # alias for the same address (docs/lxxxx-jt-aliases.md), which may
            # itself be suffixed `_cNN` when the offset recurs across segments.
            keys = ["jt%d" % n]
            if tgt:
                keys.append("l%04x" % tgt[1])
            if any(h == k or h.startswith(k + "_") for h in have for k in keys):
                continue
            missing.append(n)
        if args.min_clines and clen < args.min_clines:
            continue
        if args.skip_todo and re.search(
                r"TODO|skeleton|/\* stub|deferred|not yet lifted", body, re.I):
            continue
        if args.min_missing <= len(missing) <= args.max_missing:
            findings.append({
                "name": name, "code": code, "off": off, "line": lineno,
                "missing": missing, "want": sorted(want),
                "clen": clen, "alen": len(abody),
            })

    # Rank: a long C body that drops calls is the hand-rolled-replacement shape.
    findings.sort(key=lambda f: (len(f["missing"]), f["clen"]), reverse=True)

    print("jt_call_audit: %d annotated functions, %d comparable, %d with drops"
          % (len(funcs), len(funcs) - skipped, len(findings)))
    print("(excluded as THINK C runtime: %s)\n"
          % ", ".join("JT[%d]" % n for n in sorted(IGNORED_JT)))
    for f in findings:
        print("%-10s CODE %2d+0x%04x  boot.c:%-6d  C %4d lines / asm %4d insns"
              % (f["name"], f["code"], f["off"], f["line"], f["clen"], f["alen"]))
        print("   asm calls : %s" % ", ".join("JT[%d]" % n for n in f["want"]))
        print("   NOT in C  : %s" % ", ".join("JT[%d]" % n for n in f["missing"]))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
