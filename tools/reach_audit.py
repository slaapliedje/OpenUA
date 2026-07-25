#!/usr/bin/env python3
"""Reachability audit — find lifted bodies that can never run in a real build.

`stub_audit.py` answers "is this body written?" (`--stubs`) and "is this switch
arm filled in?" (`--arms`). Neither models REACHABILITY, and that is a real
third blind spot: a faithful, fully lifted, `REAL`-classified function that
nothing calls is counted "done" by every other tool while being dead code.
Three confirmed instances, three DIFFERENT failure modes:

  - `jt557` (the trainer, 289 lines) — no caller at all: `l0f1a` ran a port
    stand-in instead. The whole AD&D advancement path was inert while the
    audit said 1201/1206 done (2026-07-24, #78).
  - `jt556` (Human Change Class) — reachable in the call graph, but its menu
    slot `-14433` was pinned to a port-authored constant 0, so the dispatch
    arm could never be selected (2026-07-24, #82).
  - the harness case — called only from inside `#ifdef FRUA_*` blocks, i.e.
    present in a debug build and absent from the shipping one.

Hence three passes:

  --uncalled   no call site outside its own body and forward declarations
  --harness    called ONLY under #ifdef guards that are off in a default build
  --gated      called behind an A5 flag the port pins to a constant 0

## Why this is not a grep, and what the earlier attempts got wrong

Successive naive versions reported 641, then 115, then 187, then 96 "uncalled"
bodies on the same tree. Every one of those numbers was wrong. The traps:

  1. A forward declaration (`static void jt557(void);`) is not a call site.
     Counting it marks every forward-declared function "called" — how `jt557`
     hid for months — and deduping on the declaration rather than the
     definition marks working functions (`jt183`, `jt957`) as uncalled.
  2. Names in COMMENTS are not calls. `boot.c` discusses `jtNNN` / `lXXXX` in
     prose constantly (house style), so comments must be stripped or nothing
     looks dead. Same for `PROBE("jtN")` string literals.
  3. **Do not enumerate callers from a function table.** The 96-entry version
     collected call sites only from `static` definitions (`stub_audit`'s
     `FUNC_RE` is anchored on `^static`), so every call made by a NON-static
     function was invisible — which falsely condemned `l07dc` (the phase-6
     play loop calls it), `jt919`, `jt931` and `jt989`. This version scans the
     whole translation unit instead and never relies on having parsed the
     caller.
  4. `__attribute__((unused))` is the author already saying "known unused".
     Those are reported separately, not as findings.

Report candidates with evidence and check them by hand before quoting a count.

Usage:
    reach_audit.py                 # summary of all three passes
    reach_audit.py --uncalled [--all]
    reach_audit.py --harness
    reach_audit.py --gated
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stub_audit import SRC, load, parse_funcs, classify   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A lifted-function name: jtNNN or lXXXX (CLAUDE.md naming). Port-authored
# helpers (cg_*, port_*, qd_*) are out of scope — "unused" there is a
# tidiness question, not a lift gap.
NAME_RE = re.compile(r"^(jt\d+|l[0-9a-f]{4}(?:_c\d+)?)$")

# Guards that are OFF in a default `make`: the debug/harness family.
HARNESS_RE = re.compile(r"^(FRUA_|.*_?(TRACE|DIAG|TEST|BENCH|REPRO)$)")

# An A5 slot reference, in EVERY spelling boot.c uses: the bare `g_a5_14433`
# macro and the typed accessors `g_a5_byte(-14433)` / `_word` / `_long` /
# `_ptr` / `_buf` / `_chars` / `_shorts` / `_longs`. Covering only `byte` was a
# false-positive factory: `-27928` (the party head) is written as
# `g_a5_long(-27928) = ...` all over the file, so a byte-only assignment count
# saw its two `= 0` pins as the ONLY writes and reported the party head as
# permanently disabled.
_SLOT = r"\bg_a5_(?:byte|word|long|ptr|buf|chars|shorts|longs)?\(?-?(\d+)\)?"
PIN_RE = re.compile(_SLOT + r"\s*=\s*0\s*;")
ASSIGN_RE = re.compile(_SLOT + r"\s*=\s*(?!=)")
GUARD_RE = re.compile(_SLOT + r"\s*==\s*0")


def _wrap(text, width):
    out, line = [], ""
    for w in text.split():
        if line and len(line) + 1 + len(w) > width:
            out.append(line)
            line = w
        else:
            line = (line + " " + w).strip()
    if line:
        out.append(line)
    return out


def blank_noncode(text):
    """Replace comments and string literals with spaces, PRESERVING newlines
    so every match keeps its true line number."""
    def sub(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    text = re.sub(r"/\*.*?\*/", sub, text, flags=re.S)
    text = re.sub(r"//[^\n]*", sub, text)
    text = re.sub(r'"(?:[^"\\\n]|\\.)*"', sub, text)
    text = re.sub(r"'(?:[^'\\\n]|\\.)*'", sub, text)
    return text


def guard_map(lines):
    """guard_map[i] = tuple of #if defines in force at 0-based line i."""
    out, stack = [], []
    for ln in lines:
        s = ln.strip()
        m = re.match(r"#\s*if(?:n?def)?\s+(?:defined\s*\(\s*)?(\w+)", s)
        if m:
            stack.append(m.group(1))
            out.append(tuple(stack))
            continue
        if re.match(r"#\s*endif", s):
            out.append(tuple(stack))
            if stack:
                stack.pop()
            continue
        if re.match(r"#\s*el(?:se|if)", s):
            out.append(tuple(stack))
            if stack:
                stack[-1] = "!" + stack[-1].lstrip("!")
            continue
        out.append(tuple(stack))
    return out


def decl_line(line, name):
    """True if this line is a forward declaration of `name`, not a call."""
    s = line.strip()
    if not s.startswith(("static", "extern")):
        return False
    s = re.sub(r"__attribute__\s*\(\(.*?\)\)", "", s)
    return bool(re.match(r".*\b%s\s*\(" % re.escape(name), s)) and s.endswith(";")


def external_refs():
    """Names referenced from other translation units (legitimate roots)."""
    out = set()
    for dirpath, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs
                   if d not in (".git", "data", "build", "docs", "tools")]
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            p = os.path.join(dirpath, f)
            if os.path.abspath(p) == os.path.abspath(SRC):
                continue
            try:
                body = blank_noncode(open(p, errors="replace").read())
            except OSError:
                continue
            for m in re.finditer(r"\b(jt\d+|l[0-9a-f]{4}(?:_c\d+)?)\b", body):
                out.add(m.group(1))
    return out


def scan(path=SRC):
    """{name: dict(status, shipping[], harness[], unused_attr)} for every
    jt/l DEFINITION in boot.c."""
    lines = load(path)
    code = blank_noncode("\n".join(lines)).splitlines()
    guards = guard_map(lines)
    defs = {}
    for name, sig, op, cl in parse_funcs(lines):
        if NAME_RE.match(name):
            defs[name] = (sig, op, cl)
    ext = external_refs()

    out = {}
    for name, (sig, op, cl) in sorted(defs.items()):
        call = re.compile(r"\b%s\s*\(" % re.escape(name))
        addr = re.compile(r"\b%s\b\s*(?!\()" % re.escape(name))
        ship, harn = [], []
        for i, cline in enumerate(code):
            # Skip from the SIGNATURE line, not from the opening brace: when
            # `{` sits on its own line, sig < op, and the signature itself
            # matches `name(` — counting it makes EVERY function look called
            # and the whole audit silently reports zero findings.
            if sig <= i <= cl:                # own signature + body (recursion)
                continue
            if not (call.search(cline) or addr.search(cline)):
                continue
            if decl_line(lines[i], name):     # forward declaration
                continue
            g = guards[i] if i < len(guards) else ()
            live = [x for x in g if HARNESS_RE.match(x.lstrip("!"))]
            (harn if live else ship).append((i + 1, "/".join(g)))
        out[name] = {
            "status": classify(lines, op, cl),
            "line": sig + 1,
            "shipping": ship,
            "harness": harn,
            "external": name in ext,
            "unused_attr": any(
                "__attribute__((unused))" in lines[k]
                for k in range(max(0, sig - 2), min(len(lines), op + 1))),
        }
    return out


DIS = os.path.join(ROOT, "data", "work", "disasm")

# A5 pins that ARE deliberate, with the reason. Without this, every run makes
# someone re-derive the same analysis from the disassembly. Add an entry only
# after checking what the Mac's non-zero store actually means.
TRIAGED_PINS = {
    "1314": "Color QuickDraw present. The Mac's non-zero store is the "
            "NO-Color-QuickDraw path (CODE 4 @0x44fc/0x4508, off the JT[1025] "
            "environment probe); VIDEL/AGA always have the equivalent, so the "
            "port pins it. Mono uses -1315/-1318, not this. See "
            "color_mode_init() in boot.c.",
}


def mac_writes_nonzero(off):
    """Does the MAC ever store a non-zero to A5 offset -off?

    This is the discriminator that makes `--gated` usable. A port pin to 0 is
    only a DEFECT if the Mac computes a value there; if the Mac also only ever
    `clr`s the slot, the pin is faithful. Checked by hand for all 8 candidates
    at HEAD and 7 were faithful `clr`-only — doing that by hand every run is
    how a pass like this rots into noise.

    Returns None when the disassembly is not staged (data/ is git-ignored, so
    CI and a fresh clone have no listings and the answer is "unknown", not
    "no").
    """
    if not os.path.isdir(DIS):
        return None
    # NOTE the %% — `%a5` is literal text inside a %-format string.
    pat = re.compile(r"%%a5@\(-%d\)" % off)
    store = re.compile(r"\b(?:move[bwl]|st)\b[^;]*,\s*%%a5@\(-%d\)" % off)
    clr = re.compile(r"\bclr[bwl]?\s+%%a5@\(-%d\)" % off)
    for name in sorted(os.listdir(DIS)):
        if not name.endswith(".s"):
            continue
        try:
            fh = open(os.path.join(DIS, name), errors="replace")
        except OSError:
            continue
        with fh:
            for line in fh:
                if not pat.search(line):
                    continue
                if clr.search(line):
                    continue
                if store.search(line):
                    return True
    return False


def gated(path=SRC):
    """A5 flags the port only ever assigns 0, that are also used as a guard."""
    lines = load(path)
    text = "\n".join(blank_noncode("\n".join(lines)).splitlines())
    assigned, pinned, guards = {}, {}, {}
    for m in ASSIGN_RE.finditer(text):
        assigned[m.group(1)] = assigned.get(m.group(1), 0) + 1
    for m in PIN_RE.finditer(text):
        pinned[m.group(1)] = pinned.get(m.group(1), 0) + 1
    for m in GUARD_RE.finditer(text):
        guards[m.group(1)] = guards.get(m.group(1), 0) + 1
    code = blank_noncode("\n".join(lines)).splitlines()
    out = []
    for off, n in sorted(pinned.items(), key=lambda kv: -kv[1]):
        if assigned.get(off, 0) != n or off not in guards:
            continue
        where = [i + 1 for i, ln in enumerate(code)
                 if PIN_RE.search(ln) and re.search(r"\b%s\b" % off, ln)]
        out.append((off, n, guards[off], where[:4],
                    mac_writes_nonzero(int(off))))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--uncalled", action="store_true")
    ap.add_argument("--harness", action="store_true")
    ap.add_argument("--gated", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="include non-REAL bodies and known-unused ones")
    args = ap.parse_args(argv)
    both = not (args.uncalled or args.harness or args.gated)

    info = scan()
    dead = {k: v for k, v in info.items()
            if not v["shipping"] and not v["harness"] and not v["external"]}
    honly = {k: v for k, v in info.items()
             if not v["shipping"] and not v["external"] and v["harness"]}

    if args.uncalled or both:
        real = {k: v for k, v in dead.items()
                if v["status"] == "REAL" and not v["unused_attr"]}
        marked = {k: v for k, v in dead.items() if v["unused_attr"]}
        print("== NO call site anywhere (%d of %d jt/l definitions) ==" %
              (len(dead), len(info)))
        print("   %d REAL and NOT marked __attribute__((unused))  <- findings"
              % len(real))
        print("   %d already marked __attribute__((unused))       "
              "<- author knows\n" % len(marked))
        for k, v in sorted(real.items()):
            print("   %-14s line %-6d %s" % (k, v["line"], v["status"]))
        if args.all:
            print("\n   -- marked unused --")
            print("   " + ", ".join(sorted(marked)))
        print()

    if args.harness or both:
        real = {k: v for k, v in honly.items() if v["status"] == "REAL"}
        print("== called ONLY from #ifdef harness blocks (absent from a "
              "default build) ==")
        print("   %d total, %d REAL\n" % (len(honly), len(real)))
        for k, v in sorted(real.items()):
            g = sorted({g for _l, g in v["harness"]})
            print("   %-14s line %-6d under %s" % (k, v["line"], ", ".join(g)))
        print()

    if args.gated or both:
        g = gated()
        print("== A5 gates the port pins to a constant 0 ==")
        print("   A pin is only a DEFECT if the Mac computes a value there.\n")
        sus = [r for r in g if r[4] is True and r[0] not in TRIAGED_PINS]
        tri = [r for r in g if r[4] is True and r[0] in TRIAGED_PINS]
        ok = [r for r in g if r[4] is False]
        unk = [r for r in g if r[4] is None]
        for tag, rows in (("MAC STORES NON-ZERO  <- findings", sus),
                          ("mac stores non-zero, but TRIAGED as deliberate",
                           tri),
                          ("mac only clears it too — faithful", ok),
                          ("disasm not staged — unknown", unk)):
            if not rows:
                continue
            print("   %s" % tag)
            for off, pins, uses, where, _mac in rows:
                print("      -%-7s pinned %dx, guards %dx   lines %s"
                      % (off, pins, uses, ", ".join(map(str, where)) or "?"))
                if off in TRIAGED_PINS:
                    for ln in _wrap(TRIAGED_PINS[off], 66):
                        print("               %s" % ln)
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
