"""Count `JT[N]` callsites across the disassembly listings.

A static surrogate for "how busy is each JT entry on the boot path."
A real runtime trace would be more accurate but the Mac build's
binary is dominated by a few stable leaf entries (jt3 / jt384 /
jt488 / etc.), and the static frequency tracks runtime frequency
closely enough to drive lift prioritisation.

For each `JT[N]` annotation dis68k.py emits in the CODE_NN.s
listings, this tool tallies the call count, then optionally
cross-references against src/engine/boot.c to flag which entries
are already lifted and which are PROBE-only stubs.

Usage:
    jt_freq.py [--disasm DIR] [--boot SRC] [--limit N]
"""

import argparse
import os
import re
import sys
from collections import Counter
from dataclasses import dataclass
from typing import Optional


JT_CALL = re.compile(r"\(JT\[(\d+)\]\)")
# Forms we recognise in src/engine/boot.c:
#   static <ret> jtN(args) { PROBE("jtN"); ... return 0; }    → stub
#   static <ret> jtN(args) { PROBE("jtN"); ...real... }       → lifted
# Non-greedy: a greedy [^=]* would skip past the function name and
# pick up the next jtN literal in PROBE("...") or the next decl.
JT_DECL  = re.compile(r"^static\s+[^=]*?\bjt(\d+)\s*\(", re.MULTILINE)


@dataclass(frozen=True)
class JtFreq:
    jt:    int
    count: int
    state: str   # "lifted", "stub", "unknown"


def count_callsites(disasm_dir: str) -> Counter:
    """Walk every .s listing under `disasm_dir` and count JT[N] hits."""
    counts: Counter = Counter()
    if not os.path.isdir(disasm_dir):
        return counts
    for name in sorted(os.listdir(disasm_dir)):
        if not (name.startswith("CODE_") and name.endswith(".s")):
            continue
        with open(os.path.join(disasm_dir, name), encoding="utf-8") as f:
            for line in f:
                for m in JT_CALL.finditer(line):
                    counts[int(m.group(1))] += 1
    return counts


def classify_stubs(boot_src: Optional[str]) -> dict:
    """Return {jt: 'lifted' | 'stub'} for every jtN defined in boot.c.

    A jtN definition is "lifted" if its body is more than the canonical
    PROBE-only shape. We approximate with a simple heuristic: scan the
    block between the opening `{` and the matching `}` and call it lifted
    if it contains anything more interesting than PROBE + (void)arg casts
    + a single `return` of a constant.
    """
    if not boot_src or not os.path.exists(boot_src):
        return {}
    with open(boot_src, encoding="utf-8") as f:
        text = f.read()

    state: dict = {}
    for m in JT_DECL.finditer(text):
        jt = int(m.group(1))
        # Find the next `{` and its matching `}` (single-level — every
        # jtN body in this codebase opens at most one nested block we
        # care about; the PROBE macro doesn't introduce braces).
        brace = text.find("{", m.end())
        if brace < 0:
            continue
        depth = 0
        end = brace
        for i in range(brace, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        body = text[brace + 1:end]
        # Strip the canonical stub furniture and see if anything's left.
        stripped = body
        stripped = re.sub(r'PROBE\("jt\d+"\);?', "", stripped)
        stripped = re.sub(r"\(void\)\w+;\s*", "", stripped)
        stripped = re.sub(r"return\s+[-\w]+;?\s*", "", stripped)
        stripped = re.sub(r"\s+", "", stripped)
        state[jt] = "stub" if stripped == "" else "lifted"
    return state


def build_freq_table(counts: Counter, classes: dict, limit: int):
    rows = []
    for jt, count in counts.most_common(limit):
        state = classes.get(jt, "unknown")
        rows.append(JtFreq(jt=jt, count=count, state=state))
    return rows


def render_markdown(rows):
    out = ["| JT  | Callsites | State |",
           "|----:|----------:|:------|"]
    for r in rows:
        out.append(f"| {r.jt:>3} | {r.count:>9} | {r.state} |")
    return "\n".join(out)


def render_plain(rows):
    out = []
    for r in rows:
        out.append(f"JT[{r.jt:>4}]  {r.count:>5}  {r.state}")
    return "\n".join(out)


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--disasm", default="data/work/disasm",
                   help="directory holding CODE_NN.s listings")
    p.add_argument("--boot", default="src/engine/boot.c",
                   help="source to scan for lifted-vs-stub status")
    p.add_argument("--limit", type=int, default=40,
                   help="top-N JT entries to print (0 = all)")
    p.add_argument("--format", choices=("markdown", "plain"),
                   default="plain")
    args = p.parse_args(argv)

    counts = count_callsites(args.disasm)
    if not counts:
        print(f"no JT callsites under {args.disasm} — "
              "regenerate disasm with tools/dis68k.py first")
        return 1

    classes = classify_stubs(args.boot)
    limit = args.limit if args.limit > 0 else None
    rows = build_freq_table(counts, classes, limit)

    if args.format == "markdown":
        print(render_markdown(rows))
    else:
        print(render_plain(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
