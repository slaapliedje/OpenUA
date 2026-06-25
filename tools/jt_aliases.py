#!/usr/bin/env python3
"""Generate docs/jt-aliases.md — the LIVE jt<->alias map.

Every JT export jtN sits at a fixed `CODE seg + 0xOFF`; the CODE-local name for
that same address is `lOFF` (the `lXXXX` convention).  So jt521 @ CODE 14+0x6836
== l6836, jt857 @ CODE 18+0x77a0 == l77a0, etc.  We hiccup on this translation
constantly while lifting, so this doc makes it a lookup, not a derivation.

It also records, for each entry, the name the function is actually DEFINED under
in src/engine/boot.c and that definition's LINE NUMBER — which doubles as the
forward-declaration oracle: if you're calling jtX from a site ABOVE jtX's
definition line, you need a `static ... jtX(...);` forward decl first.

    python3 tools/jt_aliases.py        # writes docs/jt-aliases.md

Regenerated from the disassembly jump table + boot.c, so it never drifts.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JT = os.path.join(ROOT, "data/work/disasm/jumptable.txt")
BOOT = os.path.join(ROOT, "src/engine/boot.c")
OUT = os.path.join(ROOT, "docs/jt-aliases.md")

# 1. jumptable.txt: "  JT[ 521]  A5+0x1098  CODE 14+0x6836"
jt_addr = {}   # n -> (seg:int, off:int)
for line in open(JT):
    m = re.search(r'JT\[\s*(\d+)\]\s+A5\+0x[0-9a-fA-F]+\s+CODE\s+(\d+)\+0x([0-9a-fA-F]+)', line)
    if m:
        jt_addr[int(m.group(1))] = (int(m.group(2)), int(m.group(3), 16))

# 2. boot.c: map every DEFINED function name -> (line, is_stub).
#    A definition is `static <type> NAME(...)` whose body follows (not a
#    forward decl ending in ';').  PROBE-only one-liners are flagged stub.
boot_lines = open(BOOT).read().split('\n')
defn = {}       # name -> (1-based line, is_stub)
def_re = re.compile(r'^static\s+[A-Za-z].*?\b(jt\d+|l[0-9a-f]{4}(?:_c\d+)?)\s*\(')
stub_re = re.compile(r'\b(jt\d+|l[0-9a-f]{4}(?:_c\d+)?)\s*\([^)]*\)\s*'
                     r'(?:__attribute__\(\(unused\)\)\s*)?\{\s*PROBE\("[^"]*"\);\s*\}')
for i, ln in enumerate(boot_lines):
    m = def_re.match(ln)
    if not m:
        continue
    name = m.group(1)
    stripped = ln.rstrip()
    # forward decl if it ends in ';' and has no '{'
    if stripped.endswith(';') and '{' not in ln:
        continue
    if name in defn:        # keep the FIRST real definition
        continue
    is_stub = bool(stub_re.search(ln))
    defn[name] = (i + 1, is_stub)

# 3. For each JT entry, work out the alias + how boot.c defines it.
def classify(n, seg, off):
    alias = f"l{off:04x}"
    alias_c = f"{alias}_c{seg}"
    jt = f"jt{n}"
    # which name is the function defined under?
    for cand in (jt, alias_c, alias):
        if cand in defn:
            line, stub = defn[cand]
            return alias, cand, line, ("stub" if stub else "lifted")
    return alias, None, None, "missing"

rows = []
for n in sorted(jt_addr):
    seg, off = jt_addr[n]
    alias, defname, line, status = classify(n, seg, off)
    rows.append((n, seg, off, alias, defname, line, status))

# 4. Emit grouped by CODE segment.
icon = {"lifted": "✅", "stub": "🟡", "missing": "⬜"}
out = []
out.append("# jt ↔ alias map (auto-generated)\n")
out.append("Regenerate: `python3 tools/jt_aliases.py`. **Do not hand-edit.**\n")
out.append("Every JT export `jtN` lives at `CODE seg + 0xOFF`; its CODE-local "
           "alias is `lOFF`. This table is the lookup so we stop re-deriving it "
           "mid-lift.\n")
out.append("- **Alias** = the `lXXXX` name for the same address (segment shown "
           "for the `_cNN` collision cases).\n"
           "- **Defined as** / **line** = the name + line the function is "
           "actually defined under in `src/engine/boot.c`. The line is the "
           "**forward-decl oracle**: calling `jtX` from above its definition "
           "line needs a `static … jtX(…);` first.\n"
           "- **Status**: ✅ lifted · 🟡 PROBE stub · ⬜ no definition (may be "
           "lifted under a different alias, or genuinely absent).\n")
tot = {"lifted": 0, "stub": 0, "missing": 0}
for r in rows:
    tot[r[6]] += 1
out.append(f"**Totals:** {len(rows)} JT entries — {tot['lifted']} lifted, "
           f"{tot['stub']} stub, {tot['missing']} no-boot.c-def.\n")

by_seg = {}
for r in rows:
    by_seg.setdefault(r[1], []).append(r)
for seg in sorted(by_seg):
    out.append(f"\n## CODE {seg}\n")
    out.append("| JT | addr | alias | defined as | line | status |")
    out.append("|---|---|---|---|---:|:--:|")
    for n, _seg, off, alias, defname, line, status in by_seg[seg]:
        da = defname or "—"
        ln = str(line) if line else "—"
        out.append(f"| jt{n} | +0x{off:04x} | {alias} | {da} | {ln} | "
                   f"{icon[status]} |")

open(OUT, "w").write("\n".join(out) + "\n")
print(f"wrote {OUT}: {len(rows)} entries "
      f"({tot['lifted']} lifted / {tot['stub']} stub / {tot['missing']} no-def)")
