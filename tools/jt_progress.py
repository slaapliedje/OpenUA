#!/usr/bin/env python3
"""
jt_progress.py — the JT-lift scoreboard generator.

Porting strategy: lift the most-CALLED jump-table entries first (the
load-bearing foundation), then the stand-ins fall away and everything
above composes. This tool measures that, honestly and reproducibly, so
the progress file can never drift from the actual source:

  1. Count how often each JT[N] is *called* across the whole Macintosh
     decompilation (data/work/disasm/CODE_*.s, from dis68k's (JT[N])
     annotations).
  2. For each entry, find its `jtN` definition in src/engine/boot.c and
     classify it:
        LIFTED  — a real faithful body
        STUB    — a one-line PROBE/return placeholder, real work pending
        NOOP    — faithful AS a stub (the Mac body is empty / a constant)
        MISSING — no jtN symbol in boot.c yet
  3. Emit docs/jt-lift-progress.md: an overall tally, a per-100-band
     summary (top 100, next 100, ...), and the ranked detail table.

Usage:
    python3 tools/jt_progress.py                # regenerate the doc
    python3 tools/jt_progress.py --check        # print summary only
    python3 tools/jt_progress.py --band 2       # detail for the 2nd 100

The classifier is a heuristic (see classify()); the NOOP whitelist and a
few known alias lifts are listed explicitly. Treat band tallies as +/-2,
not exact — but the trend and the foundation coverage are reliable.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISASM = os.path.join(ROOT, "data", "work", "disasm")
BOOT_C = os.path.join(ROOT, "src", "engine", "boot.c")
OUT_MD = os.path.join(ROOT, "docs", "jt-lift-progress.md")

# Faithful-as-a-stub: the Mac body really is empty or a bare constant.
# Verified against the disassembly; these are DONE, not pending.
#   jt1/jt2/jt3 — the THINK C inline `switch` dispatch family (jt3 =
#           range table, jt1 = word-value list, jt2 = long-value list):
#           there is NO shared body to lift; every call site reads its
#           own inline table and emits an equivalent C switch (see
#           CLAUDE.md). The jtN function form is a permanent placeholder,
#           never called on the real path.
NOOP = {1170, 1198, 1163, 949, 3, 1, 2}

# JT entries whose body was lifted under a CODE-local (lXXXX) name or a
# differently-spelled wrapper; the JT symbol may be absent but the work
# is done. Add (jt_number: "note") as these are confirmed.
ALIAS_LIFTED = {
    449: "lifted as l2c60",
    1084: "lifted as l036a (same address, the error dialog)",
    63: "lifted as l60b4 (same address, byte->decimal scratch)",
    181: "lifted as l1806 (same address, the modal 'press a key' prompt)",
}

# Why each still-open top-100 entry is open — keeps the pending queue
# self-documenting so the next unit of work picks itself. Tags: subsystem
# (small body over an uncharted helper cluster), dispatcher (big multi-case
# switch), trap-shim (issues a Mac trap), HAL (needs a display backend).
PENDING_NOTES = {
    1177: "HAL — Mac framebuffer addressing; needs the row-blit display "
          "backend, not a transcription",
    1061: "trap-shim — issues Mac OS traps 0xa05d/0xaded/0xa9ee (Memory "
          "Manager handle-state family); needs the handle-flags shim",
    868:  "dispatcher — 771-line 25-case popup/menu hub; gates jt866/jt871/"
          "jt875. Own session",
    501:  "dispatcher — 598-line. Own session",
    21:   "dispatcher — 454-line. Own session",
    28:   "dispatcher — 307-line. Own session",
    936:  "dispatcher — 187-line. Own session",
    521:  "dispatcher — 162-line. Own session",
    52:   "dispatcher — 160-line. Own session",
    17:   "dispatcher — 111-line. Own session",
    497:  "dispatcher — 106-line. Own session",
    57:   "dispatcher — 74-line. Own session",
    866:  "gated by jt868 (the popup hub dispatches it)",
    871:  "gated by jt868 (the popup hub dispatches it)",
    875:  "gated by jt868 (the popup hub dispatches it)",
}


def call_frequency():
    """Map JT number -> static call count across all CODE segments."""
    freq = {}
    pat = re.compile(r"\(JT\[(\d+)\]\)")
    for name in sorted(os.listdir(DISASM)):
        if not name.endswith(".s"):
            continue
        with open(os.path.join(DISASM, name), errors="replace") as fh:
            for line in fh:
                for m in pat.finditer(line):
                    n = int(m.group(1))
                    freq[n] = freq.get(n, 0) + 1
    return freq


def boot_definitions():
    """
    Map jtN -> body text for every real definition (one with a `{...}`
    body, not a forward decl) in boot.c. Brace-matched extraction.
    """
    with open(BOOT_C, errors="replace") as fh:
        src = fh.read()

    defs = {}
    # match a definition header: `static <stuff> jtN(<args>) ...` that is
    # followed (possibly after an attribute / newline) by a `{`.
    hdr = re.compile(r"\bstatic\b[^\n;{]*?\bjt(\d+)\s*\(", re.MULTILINE)
    for m in hdr.finditer(src):
        n = int(m.group(1))
        # find the first `{` after the header that opens the body, but
        # bail if we hit a `;` first (that was a forward declaration).
        i = m.end()
        depth = 0
        j = i
        semicolon = src.find(";", i)
        brace = src.find("{", i)
        if brace == -1 or (semicolon != -1 and semicolon < brace):
            continue  # forward decl
        # brace-match the body
        j = brace
        depth = 0
        while j < len(src):
            c = src[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        body = src[brace + 1:j]
        # keep the LAST definition seen (defs come after forward decls)
        defs[n] = body
    return defs


def classify(n, defs):
    if n in NOOP:
        return "NOOP"
    if n not in defs:
        return "ALIAS" if n in ALIAS_LIFTED else "MISSING"
    body = defs[n]
    # strip comments, then the bookkeeping calls (PROBE / (void) / dbg_log)
    # wherever they sit — including when they share a line with the body.
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)
    body = re.sub(r"PROBE\s*\([^;]*\)\s*;", "", body)
    body = re.sub(r"\(void\)[^;]*;", "", body)
    body = re.sub(r"dbg_log(_num)?\s*\([^;]*\)\s*;", "", body)
    # remaining statements
    stmts = [s.strip() for s in re.split(r"[\n;]", body) if s.strip()]
    # A true stub returns a *constant* and ignores its inputs
    # (`return 0;`, `return -1;`, or nothing). A one-liner that returns
    # an *expression* (delegates, computes from args, reads a global) is
    # a real lift — abs, min/max, a getter, etc.
    if not stmts:
        return "STUB"
    if len(stmts) == 1 and re.match(r"^return\s*(-?\d+)?$", stmts[0]):
        return "STUB"
    return "LIFTED"


BANDS_DETAIL = 2  # emit the per-entry table for this many leading bands


def main():
    args = sys.argv[1:]
    freq = call_frequency()
    defs = boot_definitions()
    ranked = sorted(freq.items(), key=lambda kv: (-kv[1], kv[0]))

    status = {n: classify(n, defs) for n, _ in ranked}

    def tally(entries):
        t = {"LIFTED": 0, "STUB": 0, "NOOP": 0, "ALIAS": 0, "MISSING": 0}
        for n, _ in entries:
            t[status[n]] += 1
        return t

    if "--check" in args:
        t = tally(ranked[:100])
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        print(f"top-100: {done} done "
              f"({t['LIFTED']} lifted + {t['NOOP']} noop + {t['ALIAS']} alias), "
              f"{t['STUB']} stub, {t['MISSING']} missing")
        return

    if "--band" in args:
        b = int(args[args.index("--band") + 1])
        lo, hi = (b - 1) * 100, b * 100
        for rank, (n, c) in enumerate(ranked[lo:hi], start=lo + 1):
            note = ALIAS_LIFTED.get(n, "")
            print(f"{rank:4}  jt{n:<5} {c:4}  {status[n]:8} {note}")
        return

    # ---- write the markdown scoreboard ----
    total = tally(ranked)
    lines = []
    A = lines.append
    A("# JT-lift scoreboard\n")
    A("Auto-generated by `tools/jt_progress.py` — **do not hand-edit the")
    A("tables**; rerun the tool. Strategy: lift the most-CALLED jump-table")
    A("entries first (the foundation), in 100-entry bands, until all are")
    A("lifted — then glue the structures together. Regenerate:\n")
    A("```sh\npython3 tools/jt_progress.py\n```\n")
    A("Legend: **LIFTED** real body · **NOOP** faithful empty/constant ")
    A("(done) · **ALIAS** lifted under an lXXXX name (done) · **STUB** ")
    A("one-line placeholder (pending) · **MISSING** not in boot.c yet.\n")

    ndist = len(ranked)
    done_all = total["LIFTED"] + total["NOOP"] + total["ALIAS"]
    A(f"**{ndist} distinct JT entries are called.** Overall: "
      f"{done_all} done ({total['LIFTED']} lifted, {total['NOOP']} noop, "
      f"{total['ALIAS']} alias), {total['STUB']} stub, "
      f"{total['MISSING']} missing.\n")

    # per-100 band summary
    A("## Progress by band (100 most-called at a time)\n")
    A("| Band | Rank | done | lifted | noop/alias | stub | missing |")
    A("|------|------|-----:|-------:|-----------:|-----:|--------:|")
    for b in range((ndist + 99) // 100):
        seg = ranked[b * 100:(b + 1) * 100]
        if not seg:
            continue
        t = tally(seg)
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        A(f"| {b + 1} | {b * 100 + 1}–{b * 100 + len(seg)} | "
          f"**{done}/{len(seg)}** | {t['LIFTED']} | "
          f"{t['NOOP'] + t['ALIAS']} | {t['STUB']} | {t['MISSING']} |")
    A("")

    # detail tables for the leading bands
    for b in range(BANDS_DETAIL):
        seg = ranked[b * 100:(b + 1) * 100]
        if not seg:
            break
        A(f"## Band {b + 1} detail (rank {b*100+1}–{b*100+len(seg)})\n")
        A("| rank | JT | calls | status | note |")
        A("|-----:|----|------:|--------|------|")
        for rank, (n, c) in enumerate(seg, start=b * 100 + 1):
            note = ALIAS_LIFTED.get(n, "")
            tag = status[n]
            cell = f"**{tag}**" if tag in ("STUB", "MISSING") else tag
            A(f"| {rank} | jt{n} | {c} | {cell} | {note} |")
        A("")

    A("## The pending queue (top-100 stubs + missing, by call count)\n")
    A("Each carries _why_ it is still open, so the next unit of work is")
    A("self-selecting. Categories: **subsystem** (small body, but gated on")
    A("an uncharted multi-function cluster — lift the cluster first);")
    A("**dispatcher** (large multi-case switch, a session on its own);")
    A("**trap-shim** (issues a Mac OS trap the HAL must route); **HAL**")
    A("(needs a display/row-blit backend, not a transcription).\n")
    pend = [(n, c) for n, c in ranked[:100]
            if status[n] in ("STUB", "MISSING")]
    if not pend:
        A("_None — the top 100 are fully lifted._\n")
    else:
        for n, c in pend:
            note = PENDING_NOTES.get(n, "")
            extra = f" — {note}" if note else ""
            A(f"- jt{n} ({c} calls) — {status[n].lower()}{extra}")
        A("")

    with open(OUT_MD, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    t = tally(ranked[:100])
    done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
    print(f"wrote {OUT_MD}")
    print(f"top-100: {done}/100 done, {t['STUB']} stub, {t['MISSING']} missing")


if __name__ == "__main__":
    main()
