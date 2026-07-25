#!/usr/bin/env python3
"""Mac 1.0 vs Mac 1.2 oracle differ — locate the 1.2 bug fixes, function by function.

ADR-0017 decision 7 keeps Mac 1.2 as a per-function *oracle* rather than a
retarget: when chasing a bug, diff that one function 1.0 vs 1.2 and port just
the fix. This is the tool that makes that practical.

**Never diff these forks byte-wise.** 1.2 removes one jump-table entry
(1208 -> 1207), so every `jsr %a5@(...)` operand above it shifts and 22 of 23
CODE segments "differ" without a single instruction changing — 11,872 bytes of
pure noise. This tool compares MNEMONIC streams with operands dropped, which
reduces the whole release to 32 real hunks in 10 segments (see
docs/function-audit-2026-07-24.md §5).

Inputs are the two disassembly trees:

    python3 tools/dis68k.py data/work/UnlimitedAdventures.rfork
    python3 tools/dis68k.py data/work/UnlimitedAdventures-1.2.rfork \
            --out data/work/disasm-1.2

**Read the OPERANDS of an insert/delete hunk, not just its position.** The
aligner matches on mnemonics alone, so when the inserted instruction shares its
mnemonic with a neighbour the reported insertion point can land on the wrong
one. Hunk 31 is the example: 1.2 adds `clrb %a5@(-4943)` next to an existing
`clrb %a5@(-4945)` that 1.0 already has, and the hunk points at the -4945 clear
as "inserted". Both are `clrb`; only the operand distinguishes them.

Usage:
    mac12_diff.py --list                 # the hunk table
    mac12_diff.py --hunk 12 [-C 14]      # one hunk, both sides, in context
    mac12_diff.py --seg 21               # every hunk in one segment
    mac12_diff.py --operands             # meaningful operand-only changes
    mac12_diff.py --frameslots           # frame-slot fixes vs re-layout churn
"""
import argparse
import difflib
import os
import re
import subprocess
import sys

DIS_10 = "data/work/disasm"
DIS_12 = "data/work/disasm-1.2"
SRC = "src/engine/boot.c"

# `  4816:  4ebaf2da              jsr %pc@(L3af2)`
#
# The hex blob is ONE contiguous field of arbitrary length, and the mnemonic may
# start with '.' (`.short` for words objdump won't decode). Two earlier attempts
# at this regex were both wrong and both produced plausible-looking hunk counts:
# `(?:[0-9a-f]{2,4}\s+)+` cannot consume an 8-digit word like `d06effe8`, so
# every long-form instruction vanished; making that group optional was worse,
# because then a hex word beginning a..f (`b0280020`) matched the MNEMONIC
# group. Anchor the blob as one field and require whitespace before the
# mnemonic.
INSN = re.compile(r"^\s*([0-9a-f]{4,8}):\s+([0-9a-f]+(?:\s+[0-9a-f]+)*)\s+"
                  r"(\.?[a-z][a-z0-9._]*)(\s.*)?$")
LABEL = re.compile(r"^(L[0-9a-f]{4,8}|entry_jt\d+)\b")


class Listing:
    """One CODE_NN.s listing, as a mnemonic stream plus per-instruction context."""

    def __init__(self, path):
        self.path = path
        self.ops = []        # mnemonic per instruction
        self.rows = []       # (addr, label, full source line)
        label = "(head)"
        for raw in open(path, errors="replace"):
            m_lbl = LABEL.match(raw)
            if m_lbl:
                label = m_lbl.group(1)
            m = INSN.match(raw)
            if m:
                self.ops.append(m.group(3))
                self.rows.append((int(m.group(1), 16), label, raw.rstrip()))

    def label_at(self, i):
        return self.rows[i][1] if 0 <= i < len(self.rows) else "(end)"

    def addr_at(self, i):
        return self.rows[i][0] if 0 <= i < len(self.rows) else -1


def segments():
    for i in range(1, 23):
        a = os.path.join(DIS_10, f"CODE_{i:02d}.s")
        b = os.path.join(DIS_12, f"CODE_{i:02d}.s")
        if os.path.exists(a) and os.path.exists(b):
            yield i, a, b


def hunks():
    """[(n, seg, tag, a_lo, a_hi, b_lo, b_hi, la, lb, Listing_a, Listing_b)]"""
    out, n = [], 0
    for seg, pa, pb in segments():
        la, lb = Listing(pa), Listing(pb)
        if la.ops == lb.ops:
            continue
        sm = difflib.SequenceMatcher(None, la.ops, lb.ops, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal":
                continue
            n += 1
            out.append((n, seg, tag, i1, i2, j1, j2, la, lb))
    return out


# Operand noise that MUST be normalised away before an operand-level compare.
# Everything here moves for structural reasons, not because SSI changed anything:
#   - `%a5@(7610)` — a POSITIVE A5 displacement is a jump-table slot, and 1.2
#     removed one entry, so every slot above it shifts by 8. (A NEGATIVE A5
#     displacement is an A5-world global; DATA/DREL are byte-identical between
#     the releases, so those are meaningful and are NOT normalised.)
#   - `L1a2c` / `0x1a2c` — branch targets and CREL-relocated absolutes shift
#     with segment layout.
#   - `%fp@(-6)` — a frame slot. Several 1.2 functions gained a local, which
#     renumbers every slot below it: 229 differences of which ~all were this.
#     The port uses named C locals, so frame layout carries no meaning here.
_NOISE = (
    (re.compile(r"%a5@\(\s*\d+\s*\)"), "%a5@(JT)"),
    (re.compile(r"%fp@\(\s*-?\d+\s*\)"), "%fp@(F)"),
    (re.compile(r"\b0x[0-9a-f]+\b"), "ABS"),
    (re.compile(r"\bL[0-9a-f]{4,8}\b"), "LBL"),
    (re.compile(r"linkw %fp,#-?\d+"), "linkw %fp,#F"),
    (re.compile(r";.*$"), ""),          # the annotation comment
)


def norm_operands(text):
    for pat, rep in _NOISE:
        text = pat.sub(rep, text)
    return " ".join(text.split())


def operand_hunks():
    """Instructions that align by mnemonic but whose MEANINGFUL operands differ.

    The mnemonic-stream compare is deliberately blind to operands, which is what
    makes it immune to relocation noise — but it is equally blind to a fix that
    only changes an operand, so this pass is not optional.

    NOTE it deliberately normalises `%fp@(N)` away (see `_NOISE`): several 1.2
    functions gained a local, which renumbers every slot below it and accounted
    for ~136 of the raw 229 differences. That means a genuine frame-slot FIX is
    invisible here — use `--frameslots`, which separates the two cases by
    checking whether the enclosing function's `linkw` frame size moved.
    """
    out = []
    for seg, pa, pb in segments():
        la, lb = Listing(pa), Listing(pb)
        sm = difflib.SequenceMatcher(None, la.ops, lb.ops, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != "equal":
                continue
            for k in range(i2 - i1):
                ra, rb = la.rows[i1 + k], lb.rows[j1 + k]
                ta = norm_operands(ra[2].split(None, 2)[-1])
                tb = norm_operands(rb[2].split(None, 2)[-1])
                if ta != tb:
                    out.append((seg, ra[1], ra[0], rb[0], ra[2].strip(),
                                rb[2].strip()))
    return out


LINKW = re.compile(r"linkw %fp,#(-?\d+)")
FPSLOT = re.compile(r"%fp@\(\s*(-?\d+)\s*\)")
# dis68k annotates every jump-table call: `jsr %a5@(802)  ; -> CODE 6+0x43c4  (JT[96])`
JTCALL = re.compile(r"jsr %a5@\(\s*\d+\s*\).*\(JT\[\s*(\d+)\]\)")


def call_target_changes():
    """Calls whose TARGET JT ENTRY changed — not just its slot number.

    A third blind spot, found via hunks 25/26. `--operands` normalises
    `%a5@(positive)` to "%a5@(JT)" because 1.2 removed one jump-table entry and
    every slot above it shifts by 8 — but that also erases a change of WHICH
    entry is called. In hunks 25/26 the 1.0 code calls JT[96] and 1.2 calls
    JT[99]; as raw operands (802 vs 826) that is indistinguishable from
    renumbering, and the mnemonics are both `jsr`, so neither existing pass
    reports it.

    Comparing JT INDICES does not work, and the failed attempt is worth
    recording. "One entry removed, so an index shifts by 0 or 1" gives 3651
    hits — all false. 1.2 RESTRUCTURED the table (entries added as well as
    removed), so the same function moves several indices: `entry_jt96` is
    CODE 6+0x43c4 in 1.0 and the identical export is JT[99] at CODE 6+0x43d2 in
    1.2. Nor does comparing the target's CODE SEGMENT help — the table is
    grouped by segment, so an index shift usually stays inside the same run
    (1195 of 1207 indices keep their segment).

    What works is target FUNCTION IDENTITY, robust to both index and offset
    movement: identify a target by its RANK among its segment's exported
    offsets. 1.0's JT[96] is the k-th export of CODE 6; if 1.2's JT[99] is also
    the k-th, it is the same function and the call was not retargeted.
    """
    def table(path):
        by_idx, per_seg = {}, {}
        p = os.path.join(path, "jumptable.txt")
        if not os.path.exists(p):
            return None, None
        for line in open(p):
            m = re.match(r"\s*JT\[\s*(\d+)\]\s+\S+\s+CODE\s+(\d+)\+(0x[0-9a-f]+)",
                         line)
            if m:
                idx, seg, off = int(m.group(1)), int(m.group(2)), int(m.group(3), 16)
                by_idx[idx] = (seg, off)
                per_seg.setdefault(seg, set()).add(off)
        rank = {}
        for seg, offs in per_seg.items():
            for r, off in enumerate(sorted(offs)):
                rank[(seg, off)] = r
        return by_idx, rank

    ta, ra_ = table(DIS_10)
    tb, rb_ = table(DIS_12)
    if ta is None or tb is None:
        return []

    def ident(tbl, rank, idx):
        t = tbl.get(idx)
        return None if t is None else (t[0], rank.get(t))

    out = []
    for seg, pa, pb in segments():
        la, lb = Listing(pa), Listing(pb)
        sm = difflib.SequenceMatcher(None, la.ops, lb.ops, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != "equal":
                continue
            for k in range(i2 - i1):
                rowa, rowb = la.rows[i1 + k], lb.rows[j1 + k]
                ma, mb = JTCALL.search(rowa[2]), JTCALL.search(rowb[2])
                if not (ma and mb):
                    continue
                ia, ib = int(ma.group(1)), int(mb.group(1))
                tgta, tgtb = ta.get(ia), tb.get(ib)
                if tgta is None or tgtb is None:
                    continue
                # Identical (segment, offset) => same function, whatever the
                # index did. This alone kills 9 of the 10 hits the rank test
                # produced on its own: 1.2 changed CODE 3's export SET, so a
                # rank shifts even for functions that never moved.
                if tgta == tgtb:
                    continue
                ida = ident(ta, ra_, ia)
                idb = ident(tb, rb_, ib)
                if ida is None or idb is None or ida == idb:
                    continue
                out.append((seg, rowa[1], rowa[0], rowb[0], ia, ib,
                            rowa[2].strip(), rowb[2].strip()))
    return out


def frame_slot_changes():
    """Frame-slot operand changes, split by whether the FRAME SIZE moved.

    `--operands` normalises `%fp@(N)` because a 1.2 function that gained a local
    renumbers every slot below it — pure churn. But that also hides real fixes:
    1.2 contains at least two corrections where a wrong slot was being read
    (`docs/function-audit-2026-07-24.md` §5 names `%fp@(-24)` -> `%fp@(-20)` and
    `%fp@(-7)` -> `%fp@(-9)`), and normalising them away made the tool blind to
    exactly the class of bug it was built to find.

    Frame size alone is NOT enough to tell them apart. CODE 19's `L25ce`
    (= jt893) keeps `linkw #-28` in both releases yet moves seven distinct
    slots (-20->-12, -14->-8, -16->-18, -4->-26, ...): that is the compiler
    re-laying out the frame after 1.2 restructured the function, which is
    exactly the jt893 change already ported as hunks 19-22. Reporting it as 7
    findings is noise.

    The discriminator that works is per-FUNCTION shape:
      - a re-layout permutes MANY distinct slots and usually leaves no fp
        reference untouched;
      - a real fix changes ONE slot mapping while the function's other fp
        references stay exactly where they were.
    So a function qualifies only when it has a single distinct (a_slot ->
    b_slot) mapping AND at least one unchanged fp reference elsewhere in it.

    Returns (interesting, churn), each [(seg, func_lo_addr, frame, a_addr,
    b_addr, a_text, b_text)].
    """
    per_func = {}       # key -> {"rows": [...], "maps": set(), "same": int}
    for seg, pa, pb in segments():
        la, lb = Listing(pa), Listing(pb)
        # frame size in force at each instruction index, per side
        # (frame_size, enclosing_linkw_addr) per instruction index. The addr
        # matters: grouping by nearest LABEL splits one function into blocks,
        # and a re-layout then looks like several single-mapping "fixes" —
        # jt893 leaked two that way.
        def frames(lst):
            out, cur, at = [], None, -1
            for addr, _lbl, text in lst.rows:
                m = LINKW.search(text)
                if m:
                    cur, at = int(m.group(1)), addr
                out.append((cur, at))
            return out
        fa, fb = frames(la), frames(lb)
        sm = difflib.SequenceMatcher(None, la.ops, lb.ops, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != "equal":
                continue
            for k in range(i2 - i1):
                ia, ib = i1 + k, j1 + k
                ra, rb = la.rows[ia], lb.rows[ib]
                ta, tb = ra[2], rb[2]
                sa = FPSLOT.findall(ta)
                sb = FPSLOT.findall(tb)
                if not sa:
                    continue
                key = (seg, fa[ia][1])      # enclosing function
                st = per_func.setdefault(
                    key, {"rows": [], "maps": set(), "same": 0,
                          "frame": (fa[ia][0], fb[ib][0])})
                if sa == sb:
                    st["same"] += 1
                    continue
                # ignore rows whose non-fp operands also differ; those are
                # already reported by --operands
                if norm_operands(ta.split(None, 2)[-1]) != \
                        norm_operands(tb.split(None, 2)[-1]):
                    continue
                st["maps"].update(zip(sa, sb))
                st["rows"].append(
                    (seg, ra[1], fa[ia][0], ra[0], rb[0],
                     ta.strip(), tb.strip()))

    interesting, churn = [], []
    for key, st in sorted(per_func.items()):
        if not st["rows"]:
            continue
        single = len({m for m in st["maps"] if m[0] != m[1]}) == 1
        stable = st["same"] > 0
        same_frame = st["frame"][0] == st["frame"][1]
        (interesting if (single and stable and same_frame)
         else churn).extend(st["rows"])
    return interesting, churn


def func_entry(la, i):
    """Index of the ENCLOSING FUNCTION's entry instruction, scanning back from i.

    A hunk's nearest LABEL is usually a branch target *inside* a function, not
    that function's entry, which is the root reason the label-based lift check
    misreports: hunk 14's `L3426` lives inside `l33d8`, hunk 2's `L4e3a` inside
    `l4d98`, hunk 17's `L003a` inside `jt860`. Triage needs the entry.

    THINK C opens every non-leaf function with `linkw %fp,#-N` and ends it with
    `rts`, so a function START is either a `linkw` right after a return/jump, or
    (for a leaf with no frame) the first LABELLED instruction after a return.
    Scanning for the nearest preceding `linkw` alone is not enough — a function
    with an early `unlk`+`rts` would hand back a mid-function index.
    """
    def is_start(k):
        if k == 0:
            return True
        prev = la.ops[k - 1]
        if prev not in ("rts", "rte", "jmp"):
            return False
        if la.ops[k] == "linkw":
            return True
        return la.rows[k][1] != la.rows[k - 1][1]      # a leaf, newly labelled
    for k in range(min(i, len(la.ops) - 1), -1, -1):
        if is_start(k):
            return k
    return 0


def jt_exports(seg, lo, hi):
    """JT indices whose entry point falls in [lo, hi] of this segment."""
    path = os.path.join(DIS_10, "jumptable.txt")
    if not os.path.exists(path):
        return []
    hits = []
    for line in open(path):
        m = re.match(r"\s*JT\[\s*(\d+)\]\s+\S+\s+CODE\s+(\d+)\+(0x[0-9a-f]+)",
                     line)
        if m and int(m.group(2)) == seg and lo <= int(m.group(3), 16) <= hi:
            hits.append(int(m.group(1)))
    return hits


_CODE_MARK = re.compile(r"CODE\s*(\d+)")
_SRC_TEXT = None


def _src_text():
    global _SRC_TEXT
    if _SRC_TEXT is None:
        try:
            with open(SRC, encoding="utf-8", errors="replace") as f:
                _SRC_TEXT = f.read()
        except OSError:
            _SRC_TEXT = ""
    return _SRC_TEXT


def in_boot_c(label, seg=None):
    """Where boot.c mentions this CODE-local label, SPLIT BY SEGMENT.

    Returns (hits, wrong_seg, confirmed).

    `lXXXX` labels COLLIDE across CODE segments — the same hex offset is a
    DIFFERENT function in each, which is why CLAUDE.md's naming rule says to
    match on `(CODE, offset)`. A bare name grep does not, and this function
    used to be a bare name grep: it reported CODE 10's `l26de` for the CODE 20
    hunks 27/28 and CODE 7's `l4e3a` for the CODE 6 hunk 2, and the hunk log's
    "lifted" column recorded both as `yes`. Two hunks were queued as portable
    when their enclosing function is not in the port at all.

    The port writes its provenance into the definition's doc comment
    (`/* L611c (CODE 10+0x611c) — ... */`), so a hit carrying an explicit
    `CODE <n>` marker is evidence either way. `confirmed` means some hit names
    THIS segment; unmarked hits (bare call sites) stay in `hits` as weak
    evidence but never confirm on their own.
    """
    if not os.path.exists(SRC) or not label.startswith("L"):
        return [], [], False
    off = label[1:]
    pat = rf"\b[lL]{off}\b"
    try:
        out = subprocess.run(["grep", "-nE", pat, SRC], capture_output=True,
                             text=True).stdout.splitlines()
    except OSError:
        return [], [], False
    if seg is None:
        return out[:6], [], bool(out)

    hits, wrong, confirmed = [], [], False
    for line in out:
        marks = {int(m) for m in _CODE_MARK.findall(line)}
        if not marks:
            hits.append(line)                  # bare call site — no provenance
        elif seg in marks:
            hits.append(line)
            confirmed = True
        else:
            wrong.append(line)                 # a same-offset OTHER-segment fn
    return hits[:6], wrong[:3], confirmed


def jt_lifted(idxs):
    """Which of these JT indices boot.c actually DEFINES as `jtN`.

    The label check cannot see a function the port records by its JT name
    rather than its `LXXXX` offset — hunk 17's enclosing CODE 18 `L003a` is
    lifted as `jt860`, so the label grep found only an unrelated `case 5:`
    comment. The JT export the hunk sits under is the stronger signal there.
    """
    txt = _src_text()
    return [n for n in idxs
            if re.search(rf"^static\s+[^;]*\bjt{n}\s*\(", txt, re.M)]


def show(h, ctx):
    n, seg, tag, i1, i2, j1, j2, la, lb = h
    lbl = la.label_at(i1 if i1 < len(la.rows) else len(la.rows) - 1)
    print(f"=== hunk {n}: CODE {seg}  {tag}  "
          f"1.0 {i2 - i1} insn -> 1.2 {j2 - j1} insn")
    print(f"    enclosing 1.0 label: {lbl} "
          f"(1.0 @ {la.addr_at(i1):#06x}, 1.2 @ {lb.addr_at(j1):#06x})")
    jt = jt_exports(seg, la.addr_at(max(0, i1 - 40)), la.addr_at(i1))
    if jt:
        lifted = jt_lifted(jt)
        note = f"  (lifted in boot.c: {lifted})" if lifted else ""
        print(f"    JT exports at/above the site: {jt}{note}")
    hits, wrong, confirmed = in_boot_c(lbl, seg)
    for line in hits:
        print(f"    boot.c: {line[:150]}")
    if not confirmed and hits:
        print(f"    NOTE: no boot.c hit names CODE {seg} — the matches above "
              f"are bare call sites, not proof this function is lifted.")
    for line in wrong:
        print(f"    IGNORE (other segment, same offset): {line[:120]}")
    print(f"--- 1.0 {os.path.basename(la.path)}")
    for k in range(max(0, i1 - ctx), min(len(la.rows), i2 + ctx)):
        mark = ">>" if i1 <= k < i2 else "  "
        print(f" {mark} {la.rows[k][2]}")
    print(f"+++ 1.2 {os.path.basename(lb.path)}")
    for k in range(max(0, j1 - ctx), min(len(lb.rows), j2 + ctx)):
        mark = ">>" if j1 <= k < j2 else "  "
        print(f" {mark} {lb.rows[k][2]}")
    print()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--list", action="store_true", help="summary table")
    ap.add_argument("--hunk", type=int, help="show one hunk in context")
    ap.add_argument("--seg", type=int, help="show every hunk in a segment")
    ap.add_argument("--operands", action="store_true",
                    help="instructions that differ only in a meaningful operand")
    ap.add_argument("--calltargets", action="store_true",
                    help="calls whose target JT ENTRY changed (invisible to "
                         "--operands, which treats JT slots as noise)")
    ap.add_argument("--frameslots", action="store_true",
                    help="frame-slot changes, split into real fixes (frame size "
                         "unchanged) vs renumbering churn (frame grew)")
    ap.add_argument("--triage", action="store_true",
                    help="per hunk: the ENCLOSING FUNCTION's entry and whether "
                         "the port lifted it (the portable-or-blocked question)")
    ap.add_argument("-C", "--context", type=int, default=10,
                    help="instructions of context (default 10)")
    args = ap.parse_args(argv)

    for d in (DIS_10, DIS_12):
        if not os.path.isdir(d):
            sys.exit(f"{d} missing — see docs/mac-release.md 'The 1.2 oracle'")

    if args.triage:
        hs = hunks()
        print("Enclosing FUNCTION per hunk, and whether the port lifted it.\n"
              "The entry is found by walking back to the function prologue, NOT\n"
              "by the hunk's nearest label — a label is usually a branch target\n"
              "inside a larger function (hunk 14's L3426 lives in l33d8).\n")
        print("  # | seg | hunk lbl  | fn entry  | JT at entry | lifted as")
        for n, seg, tag, i1, i2, j1, j2, la, lb in hs:
            e = func_entry(la, i1)
            ea, elbl = la.addr_at(e), la.label_at(e)
            jts = jt_exports(seg, ea, ea)
            lifted = jt_lifted(jts)
            names = ["jt%d" % x for x in lifted]
            hits, wrong, confirmed = in_boot_c(elbl, seg)
            if confirmed:
                names.append(elbl.lower())
            verdict = ",".join(names) if names else ("?" if hits else "-")
            print(" %2d | %3d | %-9s | %-9s | %-11s | %s"
                  % (n, seg, la.label_at(i1), "%s@%#06x" % (elbl, ea),
                     ",".join(str(x) for x in jts) or "-", verdict))
        print("\n`-` / `?` still needs a hand check: a function lifted under a\n"
              "port-chosen name (l33d8 for an L3426 hunk) shows no evidence here.")
        return 0

    if args.calltargets:
        rows = call_target_changes()
        print("%d calls retargeted to a DIFFERENT jump-table entry" % len(rows))
        print("   Identity = target (segment, offset), falling back to rank\n"
              "   among the segment's exports. Expect the occasional false\n"
              "   positive where 1.2 changed a segment's export set.\n")
        for seg, lbl, aa, ab, ia, ib, ta, tb in rows:
            print("CODE %-2d near %-9s  JT[%d] -> JT[%d]" % (seg, lbl, ia, ib))
            print("   1.0 @%#06x  %s" % (aa, ta))
            print("   1.2 @%#06x  %s" % (ab, tb))
        return 0

    if args.frameslots:
        interesting, churn = frame_slot_changes()
        print("%d frame-slot changes in functions whose FRAME SIZE is "
              "unchanged  <- candidate real fixes" % len(interesting))
        print("%d in functions whose frame grew (a gained local renumbers "
              "every slot below it) — churn\n" % len(churn))
        for seg, lbl, frame, aa, ab, ta, tb in interesting:
            print("CODE %-2d near %-9s  linkw #%d (same both sides)"
                  % (seg, lbl, frame if frame is not None else 0))
            print("   1.0 @%#06x  %s" % (aa, ta))
            print("   1.2 @%#06x  %s" % (ab, tb))
        return 0

    if args.operands:
        ops = operand_hunks()
        print(f"{len(ops)} operand-only changes "
              f"(mnemonics align; relocation/JT noise normalised away)\n")
        for seg, lbl, aa, ab, ta, tb in ops:
            print(f"CODE {seg:2d}  near {lbl:>9}")
            print(f"   1.0 @{aa:#06x}  {ta}")
            print(f"   1.2 @{ab:#06x}  {tb}")
        return 0

    hs = hunks()
    if args.hunk:
        for h in hs:
            if h[0] == args.hunk:
                show(h, args.context)
                return 0
        sys.exit(f"no hunk {args.hunk} (have 1..{len(hs)})")
    if args.seg:
        for h in hs:
            if h[1] == args.seg:
                show(h, args.context)
        return 0

    print(f"{len(hs)} changed instruction hunks\n")
    print("  # | seg | 1.0 label | tag     | 1.0 insn | 1.2 insn | lifted in boot.c")
    for n, seg, tag, i1, i2, j1, j2, la, lb in hs:
        lbl = la.label_at(i1 if i1 < len(la.rows) else len(la.rows) - 1)
        hits, wrong, confirmed = in_boot_c(lbl, seg)
        jtl = jt_lifted(jt_exports(seg, la.addr_at(max(0, i1 - 40)),
                                   la.addr_at(i1)))
        if confirmed:
            hit = "yes"
        elif jtl:
            hit = "jt%d" % jtl[0]              # lifted under its JT name
        elif hits:
            hit = "?"                          # name hits, none for this seg
        else:
            hit = "-"
        print(f" {n:2d} | {seg:3d} | {lbl:>9} | {tag:7} | {i2 - i1:8d} | "
              f"{j2 - j1:8d} | {hit}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
