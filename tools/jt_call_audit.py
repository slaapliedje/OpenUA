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
CDEF_RE = re.compile(r"^static\s+[^;{}]*?\b(l[0-9a-f]{3,5}|jt\d+)\s*\(")
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
        # a forward declaration, not a definition
        if line.rstrip().endswith(";"):
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


def c_called_names(body):
    return set(re.findall(r"\b(jt\d+|l[0-9a-f]{3,5})\s*\(", body))


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
        have = c_called_names(body)
        missing = []
        for n in sorted(want):
            if ("jt%d" % n) in have:
                continue
            # an lXXXX alias for the same address counts (docs/lxxxx-jt-aliases.md)
            tgt = jt.get(n)
            if tgt and ("l%04x" % tgt[1]) in have:
                continue
            missing.append(n)
        clen = body.count("\n") + 1
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
