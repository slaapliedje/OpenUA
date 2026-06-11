#!/usr/bin/env python3
"""Per-CODE-segment lift audit.

Enumerates every function entry point in a disassembled CODE segment and
classifies it against src/engine/boot.c as LIFTED / STUB / MISSING.

The crucial bit (and the source of past duplicate-effort): a CODE function
can be lifted under EITHER its local name `lXXXX` (hex address) OR its
jump-table export name `jtNNN`. This resolves both — for each function
address it checks for an `lXXXX` definition AND maps the address through
jumptable.txt to its `JT[n]`, checking `jtN` too.

Usage:
    python3 tools/seg_audit.py 18                 # audit CODE 18
    python3 tools/seg_audit.py 18 --todo          # just the unlifted worklist
    python3 tools/seg_audit.py all                # one-line summary per segment
"""
import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOT = os.path.join(ROOT, "src/engine/boot.c")
DIS  = os.path.join(ROOT, "data/work/disasm")
JT   = os.path.join(DIS, "jumptable.txt")

CTRL = {"if", "for", "while", "switch", "goto", "return", "sizeof", "do", "else"}


def find_defs(text):
    """name -> body for every `static ... name(...) { ... }` definition."""
    defs = {}
    for m in re.finditer(r"\bstatic\b[^;{}]*?\b([A-Za-z_]\w*)\s*\(", text):
        name = m.group(1)
        p = m.end() - 1
        depth = 0
        j = p
        while j < len(text):
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        k = j + 1
        while k < len(text) and text[k] in " \t\r\n":
            k += 1
        while text.startswith("__attribute__", k):
            pp = text.find("(", k)
            d = 0
            q = pp
            while q < len(text):
                if text[q] == "(":
                    d += 1
                elif text[q] == ")":
                    d -= 1
                    if d == 0:
                        break
                q += 1
            k = q + 1
            while k < len(text) and text[k] in " \t\r\n":
                k += 1
        if k >= len(text) or text[k] != "{":
            continue                      # a forward declaration, not a def
        d = 0
        q = k
        while q < len(text):
            if text[q] == "{":
                d += 1
            elif text[q] == "}":
                d -= 1
                if d == 0:
                    break
            q += 1
        defs.setdefault(name, text[k + 1:q])
    return defs


def is_stub(body):
    """A PROBE-only stub: no call, assignment, control flow, or arithmetic."""
    b = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    b = re.sub(r"//[^\n]*", "", b)
    b = re.sub(r"PROBE\s*\([^;]*\)\s*;", "", b)
    b = re.sub(r"\(\s*void\s*\)[^;]*;", "", b)
    has_call = any(mm.group(1) not in CTRL
                   for mm in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", b))
    has_assign = "=" in re.sub(r"[=!<>]=", "", b)
    has_ctrl = re.search(r"\b(if|for|while|switch|goto)\b", b)
    bret = re.sub(r"\(\s*\w[\w \t]*\)", "", b)
    has_op = re.search(r"[*/%?:|&^]|<<|>>|\+|\[", bret)
    return not (has_call or has_assign or has_ctrl or has_op)


def load_boot():
    defs = find_defs(open(BOOT).read())
    lifted = set()
    stub = set()
    for name, body in defs.items():
        (stub if is_stub(body) else lifted).add(name)
    return lifted, stub


def jt_freq():
    freq = {}
    for s in os.listdir(DIS):
        if re.fullmatch(r"CODE_\d+\.s", s):
            for m in re.finditer(r"\(JT\[(\d+)\]\)", open(os.path.join(DIS, s)).read()):
                freq[int(m.group(1))] = freq.get(int(m.group(1)), 0) + 1
    return freq


def addr_to_jt(seg):
    """hex-addr (stripped) -> JT number, for one CODE segment."""
    out = {}
    for line in open(JT):
        m = re.search(r"JT\[\s*(\d+)\]\s+A5\+0x[0-9a-f]+\s+CODE\s+%d\+0x([0-9a-f]+)" % seg, line)
        if m:
            out[m.group(2).lstrip("0") or "0"] = int(m.group(1))
    return out


def entries(seg):
    """function entry points in CODE_<seg>.s: stripped-hex-addr set."""
    lines = open(os.path.join(DIS, "CODE_%02d.s" % seg)).read().splitlines()
    out = set()
    for i, ln in enumerate(lines):
        lab = re.match(r"^L([0-9a-f]+):\s*$", ln)
        exp = re.match(r"^entry_jt(\d+):", ln)
        if lab and i + 1 < len(lines) and "linkw" in lines[i + 1]:
            out.add(lab.group(1).lstrip("0") or "0")
        elif exp and i + 1 < len(lines):
            am = re.match(r"\s*([0-9a-f]+):", lines[i + 1])
            if am:
                out.add(am.group(1).lstrip("0") or "0")
    return out


def classify(seg, lifted, stub, freq):
    a2jt = addr_to_jt(seg)
    l_lifted = {n[1:].lstrip("0") or "0" for n in lifted if re.fullmatch(r"l[0-9a-fA-F]{3,4}", n)}
    l_stub = {n[1:].lstrip("0") or "0" for n in stub if re.fullmatch(r"l[0-9a-fA-F]{3,4}", n)}
    jt_lifted = {int(n[2:]) for n in lifted if re.fullmatch(r"jt\d+", n)}
    jt_stub = {int(n[2:]) for n in stub if re.fullmatch(r"jt\d+", n)}
    rows = []
    for addr in entries(seg):
        j = a2jt.get(addr)
        f = freq.get(j, 0) if j else 0
        if addr in l_lifted or (j in jt_lifted):
            st = "LIFTED"
        elif addr in l_stub or (j in jt_stub):
            st = "STUB"
        else:
            st = "MISSING"
        name = ("jt%d" % j) if j else ("l%s" % addr)
        rows.append((st, f, addr, name))
    return rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    lifted, stub = load_boot()
    freq = jt_freq()
    arg = sys.argv[1]
    todo_only = "--todo" in sys.argv
    if arg == "all":
        for seg in range(1, 24):
            if not os.path.exists(os.path.join(DIS, "CODE_%02d.s" % seg)):
                continue
            rows = classify(seg, lifted, stub, freq)
            done = sum(1 for r in rows if r[0] == "LIFTED")
            print("CODE %2d: %3d fns  %3d lifted  %3d stub  %3d missing  (%2d%%)"
                  % (seg, len(rows),
                     done,
                     sum(1 for r in rows if r[0] == "STUB"),
                     sum(1 for r in rows if r[0] == "MISSING"),
                     100 * done // max(1, len(rows))))
        return
    seg = int(arg)
    rows = classify(seg, lifted, stub, freq)
    done = sum(1 for r in rows if r[0] == "LIFTED")
    todo = [r for r in rows if r[0] != "LIFTED"]
    print("CODE %d: %d functions, %d lifted (%d%%), %d remaining"
          % (seg, len(rows), done, 100 * done // max(1, len(rows)), len(todo)))
    todo.sort(key=lambda r: (-r[1], r[2]))
    print("\n%-7s %5s %-7s %s" % ("status", "calls", "addr", "name"))
    for st, f, addr, name in todo:
        print("%-7s %5d 0x%-5s %s" % (st, f, addr, name))


if __name__ == "__main__":
    main()
