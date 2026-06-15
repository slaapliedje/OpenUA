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
        STANDIN — a real body, but a non-faithful port reimplementation
                  (in the STANDIN whitelist); counted as pending, not done
        MISSING — no jtN symbol in boot.c yet
  3. Emit docs/jt-lift-progress.md: an overall tally, a per-100-band
     summary (top 100, next 100, ...), and the ranked detail table.

Usage:
    python3 tools/jt_progress.py                # regenerate the doc
    python3 tools/jt_progress.py --check        # print summary only
    python3 tools/jt_progress.py --band 2       # detail for the 2nd 100
    python3 tools/jt_progress.py --standins     # list the port stand-ins
    python3 tools/jt_progress.py --wiring       # asm vs port call-site audit
    python3 tools/jt_progress.py --wiring 200   # one entry's callers

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
#   jt1061 — _SwapMMUMode ($A05D): flips the 68k between 24/32-bit
#           addressing. The 68030 Falcon/TT has one flat 32-bit mode, so
#           the trap is genuinely a no-op on the target (done, not pending).
NOOP = {1170, 1198, 1163, 949, 3, 1, 2, 1061, 1130,
        329, 920, 736,   # literal `rts` / empty linkw-unlk bodies on the Mac
        252, 260, 234, 271, 326,   # band-4 bare-rts entries (raw bytes checked)
        709,   # band-5 bare-rts (CODE 16+0x0004)
        859}   # band-5 bare-rts (CODE 18+0x77f6: 4e75)

# JT entries whose body was lifted under a CODE-local (lXXXX) name or a
# differently-spelled wrapper; the JT symbol may be absent but the work
# is done. Add (jt_number: "note") as these are confirmed.
ALIAS_LIFTED = {
    50: "lifted as l5ac2 (same address; page-up -806 toggle, jt297 key 338)",
    51: "lifted as l5ad8 (same address; page-down -17443 -> jt983, jt297 key 339)",
    857: "lifted as l77a0 (same address; the -25242 hook-table walker)",
    456: "lifted as l2d3e (same address; the DLItem event poll, full lift)",
    443: "lifted as l1676 (same address; the DLItem cmd dispatcher)",
    293: "lifted as l05ca (same address; wall code on a cell edge)",
    528: "lifted as l62ec (same address; combat map-cell fetch)",
    409: "lifted as l3e0c (same address; first-index-of scan)",
    207: "lifted as l5484 (same address; map-cell edge classify)",
    449: "lifted as l2c60",
    1084: "lifted as l036a (same address, the error dialog)",
    63: "lifted as l60b4 (same address, byte->decimal scratch)",
    181: "lifted as l1806 (same address, the modal 'press a key' prompt)",
    395: "lifted as l46b2 (same address; tolower)",
    45: "lifted as l5700 (same address; slot-1 mode teardown)",
    47: "lifted as l541a (same address; resource-slot loader)",
    106: "lifted as l3880 (same address; GLIB cell blit)",
    110: "lifted as l33ac (same address; binder open, the jt81 dep)",
    193: "lifted as l4fbe (same address; see combat gateway notes)",
    364: "lifted as l6e50 (same address; clamp 0..40 -> -10374)",
    516: "lifted as l6554 (same address; creature-on-map predicate)",
    66: "lifted as l6048 (same address; 6-byte thunk -> l604e)",
    1154: "lifted as jt1154_pg (page-up toggle read, A5 -806)",
    99: "lifted as l4b84 (same address; the Mac body is just jsr JT[175])",
    1054: "the _Delete Pascal trap glue = the shim's FSDelete (jt416)",
    1028: "the _NewPtr Pascal trap glue = the shim's NewPtr (macmemory)",
    1032: "the _DisposHandle Pascal trap glue = the shim's DisposeHandle",
    1045: "the _GetVInfo PB trap glue = the shim's files.c volume calls",
    446: "lifted as l30ba (same address)",
    1162: "lifted as l3e38 (same address; idle dispatcher, L3e8e branch deferred)",
    43: "lifted as l579e (same address; bigpic backdrop load)",
    60: "lifted as l5f84 (same address)",
    68: "lifted as l604e (same address)",
    62: "lifted as l6096 (same address; -21508 node release)",
    192: "lifted as l4e3a (same address)",
    368: "lifted as l6520_c8 (same address; monster-art class)",
    532: "lifted as l635e (same address; creature-cell redraw)",
    886: "lifted as l1276 (same address; jt904 roster staging)",
    899: "lifted as l5274 (same address; max-HP ceiling)",
    105: "lifted as l3f3c (same address; bigpic palette-range install)",
    1038: "the _GetOSEvent/SetTrapAddress trap glue = the shim's events",
    1059: "the async-PB file trap glue (FSDispatch sel 9/10/16/17/11) = the shim's files.c",
    1060: "the async-PB volume trap glue = the shim's files.c",
    1062: "the _StripAddress glue — identity on the 68030's flat 32-bit bus",
    1025: "the _SysEnvirons availability glue — the shim's environment is fixed",
    1076: "lifted as l7ab4 (same address; the message paginator, full)",
    1124: "lifted as l4d88 (same address; CODE 4 rect helper)",
    988: "lifted as l17e2 (same address; the .slb loader entry)",
    48: "lifted as l5864 (same address; -24260 release + 0xFF flag)",
    514: "lifted as l6520 (same address; the CODE 14 range check)",
    950: "lifted as l0b20 (same address; the -28006 record helper)",
    1052: "the _Eject PB trap glue — no-op on a hard-disk install",
    1033: "the _HLock trap glue = the shim's HLock (macmemory)",
    670: "lifted as l48f4 (same address; the cast-announce, full)",
    24: "lifted as l2000 (same address; the STR weight-allowance table)",
    88: "lifted as l5124 (same address; the game-state reset)",
}

# STAND-INS: JT entries that DO have a body (so classify() would call them
# LIFTED) but whose body is a port reimplementation, NOT a faithful lift of
# the Mac routine. They masquerade as done and silently inflate the score —
# this list pulls them back out into their own bucket (counted as pending,
# like STUB) with the faithful target each one still needs. Add (n: "note")
# as a divergence is confirmed against the disassembly; remove once the real
# lift lands.
STANDIN = {
    # RESOLVED 2026-06-15 — both were stale annotations, not current behaviour:
    #   jt114: the CLUT band-rebase lived in l309c_tile; jt114 now routes through
    #     l309c -> l2d4e (raw tile byte = direct CLUT index, 255=transparent — the
    #     faithful Mac model). The set/handle resolution moved upstream to the
    #     binder-model jt200_layer (l37aa(jt468(group), set)). l309c_tile is now
    #     used only by the two item-portrait blits, not the walls.
    #   jt118: jt108 IS L38d0 (both = CODE 6+0x38d0). So jt118 = jt108(1)[=L38d0(1)]
    #     + jt114[blit ~= the Mac jt1001's L309c blit], matching the Mac jt118 =
    #     L38d0(1) + jt1001. Faithful.
    # (The garbled wall TILES are the separate piece-data puzzle in
    #  docs/dungeon-3d-render-state, NOT the blit.)
}

# Non-JT port stand-ins: whole port_*/lXXXX reimplementations that stand in
# for a faithful Mac path. Not keyed by a JT number, so they live here as a
# static list for visibility (kept in sync with docs/stub-inventory.md).
PORT_STANDINS = [
    ("port_draw_play_frame",
     "coarse HUD-chrome over-blit (the #114 'jank'); faithful composer is "
     "jt304 -> L3fd8 (a few jt1001 FRAME pieces + jt216/L4430 panels)"),
    ("l309c_tile",
     "the dungeon 8bpp wall-tile channel with the per-set band rebase; "
     "non-faithful colour model — see jt114 above (#129)"),
    ("port_run_encounter / port_rest / port_play_message / port_begin_adventure",
     "play-loop stand-ins over the faithful CODE 15-19 chain (l07dc -> jt918 "
     "-> jt948); being replaced piecemeal"),
    ("port_render_geo_* / port_render_topview",
     "area-map stand-ins; faithful renderers are jt501/jt521 (now lifted) — "
     "rewire the callers"),
    ("port_show_intro / port_menu_bar / port_hud_text_clut",
     "title/HUD chrome stand-ins, trace-matched but not lifted from CODE 22/21"),
    ("port_*_demo (blit/play/sprite/view/wall) + port_l6234_verify",
     "throwaway harness scaffolding, never on the faithful path"),
]


# Why each still-open top-100 entry is open — keeps the pending queue
# self-documenting so the next unit of work picks itself. Tags: subsystem
# (small body over an uncharted helper cluster), dispatcher (big multi-case
# switch), trap-shim (issues a Mac trap), HAL (needs a display backend).
PENDING_NOTES = {
    501:  "dispatcher — 598-line. Own session",
    21:   "dispatcher — 454-line. Own session",
    28:   "dispatcher — 307-line. Own session",
    936:  "dispatcher — 187-line. Own session",
    521:  "dispatcher — 162-line. Own session",
    52:   "system-tick / housekeeping dispatcher (JT[1] switch). NOT the "
          "sound hub (earlier mis-call): jt984/979/980 are the timed-"
          "callback table (jt1149=_TickCount, jt1151/jt1145=menu-bar); only "
          "a couple arms touch audio. SysBeep is jt1147 (already lifted); "
          "the real sound work is the .slb music engine (jt986/981 + codec "
          "L1a0c/jt974/jt975 + DMA HAL) — its own session",
    17:   "dispatcher — 111-line; currently a leaf stub for L01de (jt868 "
          "hub). Own session to lift the real body",
    497:  "dispatcher — 106-line. Own session",
    57:   "dispatcher — 74-line. Own session",
    866:  "CODE 18 sibling of jt868 (NOT dispatched by it). Own lift",
    871:  "CODE 18 sibling of jt868 (NOT dispatched by it). Own lift",
    875:  "CODE 18 sibling of jt868 (NOT dispatched by it). Own lift",
    327:  "dispatcher — 14-case JT[1] design-record edit, 2.2KB. Own session",
    290:  "dispatcher — the 806B editor click tool over 5 unlifted CODE 22 "
          "locals (L1240/L0ee6/L0674/L069a/L0716). Own session",
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
        # find the first `{` or `;` after the header, but skip any that sit
        # inside a comment — an inline `/* +0x1fae; id 130 */` annotation
        # carries a semicolon that must NOT read as a forward-decl terminator.
        semicolon = brace = -1
        k = i
        while k < len(src):
            c = src[k]
            if c == "/" and k + 1 < len(src) and src[k + 1] == "*":
                end = src.find("*/", k + 2)
                k = len(src) if end == -1 else end + 2
                continue
            if c == "/" and k + 1 < len(src) and src[k + 1] == "/":
                nl = src.find("\n", k + 2)
                k = len(src) if nl == -1 else nl + 1
                continue
            if c == ";":
                semicolon = k
                break
            if c == "{":
                brace = k
                break
            k += 1
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


def port_call_frequency():
    """
    Map jtN -> number of CALL sites in boot.c (every `jtN(` minus the
    definition/forward-decl headers). This is the port-side mirror of
    call_frequency(): comparing the two tells us whether a lifted entry is
    actually WIRED IN at roughly the density the Mac calls it, or whether it
    is defined-but-dangling (a body nothing invokes) or under-wired (the Mac
    reaches it from many sites, the port from few — a missing call path).
    Heuristic: calls reached through an lXXXX alias won't show here, so an
    ALIAS entry reading port=0 is expected, not a gap.
    """
    with open(BOOT_C, errors="replace") as fh:
        src = fh.read()
    freq = {}
    for m in re.finditer(r"\bjt(\d+)\s*\(", src):
        n = int(m.group(1))
        freq[n] = freq.get(n, 0) + 1
    # subtract the `static ... jtN(` definition / forward-decl headers
    for m in re.finditer(r"\bstatic\b[^\n;{]*?\bjt(\d+)\s*\(", src):
        n = int(m.group(1))
        freq[n] = freq.get(n, 0) - 1
    return freq


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
    # A real body, but flagged as a non-faithful port reimplementation.
    if n in STANDIN:
        return "STANDIN"
    return "LIFTED"


BANDS_DETAIL = 2  # emit the per-entry table for this many leading bands


def main():
    args = sys.argv[1:]
    freq = call_frequency()
    defs = boot_definitions()
    ranked = sorted(freq.items(), key=lambda kv: (-kv[1], kv[0]))

    status = {n: classify(n, defs) for n, _ in ranked}

    def tally(entries):
        t = {"LIFTED": 0, "STUB": 0, "NOOP": 0, "ALIAS": 0,
             "STANDIN": 0, "MISSING": 0}
        for n, _ in entries:
            t[status[n]] += 1
        return t

    if "--wiring" in args:
        pf = port_call_frequency()
        wi = args.index("--wiring")
        arg = args[wi + 1] if wi + 1 < len(args) else None

        if arg and arg.isdigit():
            # Targeted: one entry's asm-vs-port counts + its actual callers.
            n = int(arg)
            with open(BOOT_C, errors="replace") as fh:
                lines = fh.readlines()
            callers = []
            cpat = re.compile(r"\bjt%d\s*\(" % n)
            hpat = re.compile(r"\bstatic\b.*\bjt%d\s*\(" % n)
            for i, ln in enumerate(lines, 1):
                if cpat.search(ln) and not hpat.search(ln):
                    callers.append((i, ln.strip()[:78]))
            al = ALIAS_LIFTED.get(n)
            print(f"jt{n}: class={classify(n, defs)}  asm_calls={freq.get(n,0)}"
                  f"  port_calls={pf.get(n,0)}")
            if al:
                print(f"  ALIAS: {al}  (callers reach it via the lXXXX name)")
            print(f"  port call sites ({len(callers)}):")
            for i, txt in callers:
                print(f"    boot.c:{i}: {txt}")
            return

        # Global: STUB/STANDIN with asm weight are the REAL gaps; the broad
        # dangling/under lists are noisy (hook-table fn-pointer calls show as
        # dangling; hot utils + the JT[1/2/3] switch family are inlined, so
        # they show as under-wired) — filter those by hand or use `--wiring N`.
        gaps = sorted(
            [(n, c) for n, c in ranked if status[n] in ("STUB", "STANDIN")],
            key=lambda kv: -kv[1])
        dangling = [(n, c) for n, c in ranked
                    if status[n] in ("LIFTED", "STANDIN")
                    and pf.get(n, 0) == 0 and n not in ALIAS_LIFTED]
        under = [(n, c, pf.get(n, 0)) for n, c in ranked
                 if status[n] in ("LIFTED", "STANDIN")
                 and c >= 4 and pf.get(n, 0) * 3 < c]
        print("WIRING AUDIT (asm call sites vs port call sites).\n")
        print(f"REAL GAPS — stub/stand-in bodies the Mac calls, by asm weight "
              f"({len(gaps)}):")
        for n, c in gaps[:20]:
            print(f"  jt{n:<5} asm={c:<4} port={pf.get(n,0):<3} {status[n]}")
        print(f"\nNoisy signals (need human filtering): {len(dangling)} dangling "
              f"(many are fn-pointer/hook-table calls), {len(under)} under-wired "
              "(many are inlined hot utils / the JT[1/2/3] switch family).")
        print("Use `--wiring N` to spot-check one entry's callers.")
        return

    if "--standins" in args:
        print("JT stand-ins (real body, but a non-faithful port "
              "reimplementation):")
        for n in sorted(STANDIN):
            c = freq.get(n, 0)
            print(f"  jt{n:<5} {c:4} calls  {status.get(n, '?'):8} {STANDIN[n]}")
        print("\nNon-JT port stand-ins (whole-routine reimplementations):")
        for name, note in PORT_STANDINS:
            print(f"  {name}\n      {note}")
        return

    if "--check" in args:
        t = tally(ranked[:100])
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        print(f"top-100: {done} done "
              f"({t['LIFTED']} lifted + {t['NOOP']} noop + {t['ALIAS']} alias), "
              f"{t['STUB']} stub, {t['STANDIN']} stand-in, "
              f"{t['MISSING']} missing")
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
    A("one-line placeholder (pending) · **STANDIN** real body but a non-")
    A("faithful port reimplementation (pending) · **MISSING** not in boot.c yet.\n")

    ndist = len(ranked)
    done_all = total["LIFTED"] + total["NOOP"] + total["ALIAS"]
    A(f"**{ndist} distinct JT entries are called.** Overall: "
      f"{done_all} done ({total['LIFTED']} lifted, {total['NOOP']} noop, "
      f"{total['ALIAS']} alias), {total['STUB']} stub, "
      f"{total['STANDIN']} stand-in, {total['MISSING']} missing.\n")

    # per-100 band summary
    A("## Progress by band (100 most-called at a time)\n")
    A("| Band | Rank | done | lifted | noop/alias | stub | standin | missing |")
    A("|------|------|-----:|-------:|-----------:|-----:|--------:|--------:|")
    for b in range((ndist + 99) // 100):
        seg = ranked[b * 100:(b + 1) * 100]
        if not seg:
            continue
        t = tally(seg)
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        A(f"| {b + 1} | {b * 100 + 1}–{b * 100 + len(seg)} | "
          f"**{done}/{len(seg)}** | {t['LIFTED']} | "
          f"{t['NOOP'] + t['ALIAS']} | {t['STUB']} | {t['STANDIN']} | "
          f"{t['MISSING']} |")
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
            note = ALIAS_LIFTED.get(n, "") or STANDIN.get(n, "")
            tag = status[n]
            cell = (f"**{tag}**"
                    if tag in ("STUB", "STANDIN", "MISSING") else tag)
            A(f"| {rank} | jt{n} | {c} | {cell} | {note} |")
        A("")

    # ---- stand-ins: real bodies that are not faithful lifts ----
    A("## Stand-ins (real body, but a port reimplementation — NOT a lift)\n")
    A("These have a body so they look done, but diverge from the Mac. They")
    A("are counted as **pending**, not done, and each names the faithful")
    A("target it still needs. Verified against the disassembly; remove an")
    A("entry from `STANDIN`/`PORT_STANDINS` in the tool once the real lift")
    A("lands.\n")
    si = [(n, c) for n, c in ranked if status[n] == "STANDIN"]
    si += [(n, freq.get(n, 0)) for n in STANDIN if n not in dict(ranked)]
    if si:
        A("| JT | calls | faithful target |")
        A("|----|------:|-----------------|")
        for n, c in sorted(si, key=lambda kv: (-kv[1], kv[0])):
            A(f"| jt{n} | {c} | {STANDIN[n]} |")
        A("")
    A("Non-JT port stand-ins (whole-routine reimplementations, "
      "kept in sync with `docs/stub-inventory.md`):\n")
    for name, note in PORT_STANDINS:
        A(f"- `{name}` — {note}")
    A("")

    A("## The pending queue (top-100 stubs + stand-ins + missing, by call count)\n")
    A("Each carries _why_ it is still open, so the next unit of work is")
    A("self-selecting. Categories: **subsystem** (small body, but gated on")
    A("an uncharted multi-function cluster — lift the cluster first);")
    A("**dispatcher** (large multi-case switch, a session on its own);")
    A("**trap-shim** (issues a Mac OS trap the HAL must route); **HAL**")
    A("(needs a display/row-blit backend, not a transcription); **standin**")
    A("(a non-faithful port body — see the stand-ins section).\n")
    pend = [(n, c) for n, c in ranked[:100]
            if status[n] in ("STUB", "STANDIN", "MISSING")]
    if not pend:
        A("_None — the top 100 are fully lifted._\n")
    else:
        for n, c in pend:
            note = PENDING_NOTES.get(n, "") or STANDIN.get(n, "")
            extra = f" — {note}" if note else ""
            A(f"- jt{n} ({c} calls) — {status[n].lower()}{extra}")
        A("")

    with open(OUT_MD, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    t = tally(ranked[:100])
    done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
    print(f"wrote {OUT_MD}")
    print(f"top-100: {done}/100 done, {t['STUB']} stub, "
          f"{t['STANDIN']} stand-in, {t['MISSING']} missing")


if __name__ == "__main__":
    main()
