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
JUMPTBL = os.path.join(DISASM, "jumptable.txt")

# Chunk size for the progress summary. The campaign historically used 100
# ("bands"); 2026-06-19 (user-directed) it dropped to 50-entry chunks so each
# unit of work is smaller + easier to target. Rank ranges are absolute, so the
# old band-N references still map (band 6 == chunks 11–12 == rank 501–600).
CHUNK = 50

# CODE-segment -> subsystem label, for the "what's used where" rollup. The
# segment of every JT[N] comes from jumptable.txt; this names the ones we've
# charted. Unlabelled segments print "—" (objective counts still apply).
SEGMENT_SUBSYS = {
    1:  "boot / A5 init / entry",
    2:  "design EDITOR — event/zone/map-step editing (Step Event, Rest in "
        "Zone, Chain, col/row cursor) — AUTHORING, not the play path",
    8:  "foundational UI/file library — numeric-input fields (Valid numbers "
        "%ld-%ld), menu manager (Too many menus), file-group prefixes "
        "(DSN/GAME/SAVE/STR/STRG)",
    9:  "INVENTORY + spellbook viewer — item/spell list UI w/ pictures "
        "(Item Kind, %d Spells Memorized, Page, CPIC, Select/Cancel)",
    10: "PICTURE/sprite display — PIC/SPRIT/CPIC event & portrait images "
        "(jt1004 art primitive); overlaps the event-picture path (#125)",
    11: "design EDITOR — 3D-MAP (GEO) editing + save (Save3DMap, 'Unable to "
        "write geo') — AUTHORING, not the play path",
    3:  "Mac Toolbox shim (QuickDraw / Dialog / Event / Menu)",
    4:  "display low-level: QuickDraw/blit math, scroll-blit (jt1126), coord "
        "scale (jt1135), idle-paint (jt1134), input map (jt1125), byte-swap "
        "(jt1180/99) — MOSTLY SUPERSEDED by the VIDEL display HAL",
    5:  "the CORE runtime library — called by EVERY segment: string/number "
        "format, the error dialog (jt1084), low-level helpers (CODE 4's "
        "main consumer)",
    6:  "file-group cache + GLIB art + resource manager",
    7:  "list dialog (JT[169]) + text widgets",
    12: "Training Hall menu + roster (jt918 / l0aae / l02dc)",
    13: "area-map line/region renderer (jt501)",
    14: "area-map render tree (jt521)",
    15: "play-entry + save/load + party list (jt574..590 / l07dc)",
    16: "combat HANDLER tier — spell-effect/per-actor handlers registered "
        "into CODE 18 (code16-wall)",
    17: "character generation (jt574 / jt557 / l618c)",
    18: "combat engine (jt610 / jt856 / l4d98 / l709e)",
    19: "character sheet + party container (jt886 / jt904 / jt910)",
    20: "ENCOUNTER / combat narration + event text — 'A battle begins', "
        "'is hit FOR N points of Damage', 'dies', wish/genie events; the "
        "l709e event dispatch (in-game, combat path #115)",
    21: "SPELL MEMORIZATION + scroll scribing — the camp spell-prep screen "
        "(memorize/scribe, Cleric/Druid/Magic-User lists, 'already knows "
        "that spell') — NOT the command bar (was mislabeled)",
    22: "main menu + design select + editor tools (jt315 / jt290 / jt327)",
}


def parse_jumptable():
    """Map JT number -> CODE segment number from jumptable.txt
    (`JT[ 10]  A5+0x0070  CODE  6+0x0538`)."""
    seg = {}
    if not os.path.exists(JUMPTBL):
        return seg
    pat = re.compile(r"JT\[\s*(\d+)\]\s+A5\+\S+\s+CODE\s+(\d+)\+")
    with open(JUMPTBL, errors="replace") as fh:
        for line in fh:
            m = pat.search(line)
            if m:
                seg[int(m.group(1))] = int(m.group(2))
    return seg


def local_stub_scan():
    """Find CODE-local lXXXX helpers that are PROBE-only placeholders in
    boot.c (`static <ret> lXXXX(...) { PROBE("lXXXX"); ... }` with no real
    body) — the non-JT leaf stubs the JT scoreboard doesn't see."""
    with open(BOOT_C, errors="replace") as fh:
        src = fh.read()
    stubs = []
    hdr = re.compile(r"\bstatic\b[^\n;{]*?\b(l[0-9a-f]{3,4})\s*\(", re.MULTILINE)
    for m in hdr.finditer(src):
        name = m.group(1)
        brace = src.find("{", m.end())
        semi = src.find(";", m.end())
        if brace < 0 or (semi != -1 and semi < brace):
            continue  # forward decl
        depth, j = 0, brace
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        body = src[brace + 1:j]
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
        body = re.sub(r"//.*", "", body)
        body = re.sub(r'PROBE\s*\([^;]*\)\s*;', "", body)
        body = re.sub(r"\(void\)[^;]*;", "", body)
        body = re.sub(r"dbg_log(_num)?\s*\([^;]*\)\s*;", "", body)
        stmts = [s.strip() for s in re.split(r"[\n;]", body) if s.strip()]
        is_stub = (not stmts) or (
            len(stmts) == 1 and re.match(r"^return\s*(-?\d+)?$", stmts[0]))
        if is_stub and 'PROBE' in src[brace:j + 1]:
            stubs.append(name)
    # dedup, keep first occurrence order
    seen, out = set(), []
    for n in stubs:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out

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
        561,   # CODE 17+0x4d62: literal `rts` (char-gen no-op hook)
        859,   # band-5 bare-rts (CODE 18+0x77f6: 4e75)
        1115,  # CODE 4+0x4cb2: literal linkw/unlk/rts (live-probe audit 2026-07-04)
        956,   # CODE 21+0x326a: literal `rts` (live-probe audit 2026-07-04)
        1137,  # CODE 4+0x7a10: literal `moveq #0; rts` (live-probe audit 2026-07-04)
        510,   # CODE 13+0x6d1a: bare rts (band 6, disasm-verified)
        512,   # CODE 14+0x5d8e: bare rts (band 6, disasm-verified)
        744,   # CODE 18+0x3fb2: literal linkw/unlk/rts (band 6, disasm-verified)
        136,   # CODE 7+0x11a6: bare rts (band 7, disasm-verified)
        962,   # CODE 5+0x7cbe: bare rts (band 7, disasm-verified)
        1119,  # CODE 4+0x797e: bare rts (band 7, disasm-verified)
        1156,  # CODE 4+0x670e: bare rts (band 7, disasm-verified)
        1207,  # CODE 4+0x7e3e: bare rts (band 7, disasm-verified)
        1117,  # CODE 4+0x77ee: empty linkw/unlk (band 7, disasm-verified)
        1150,  # CODE 4+0x61fc: empty linkw/unlk (band 7, disasm-verified)
        1159,  # CODE 4+0x4350 = l4350: Palette Manager usage-hint reseed
               # + ActivatePalette — HAL-moot (the port owns the CLUT;
               # no palette object, depth pinned at 8). Documented no-op
               # of the jt1158 class (band 7 batch 14; full decode in
               # boot.c at l4350).
        # band 7 batch 18 — verified faithful-moot: each Mac body is empty,
        # a constant, or pure trap glue whose OS trap is meaningless on the
        # Falcon HAL (the jt1061 _SwapMMUMode class). Disasm-confirmed.
        9,     # CODE 1+0x3ec: THINK C exit-unpatch of _LoadSeg/_ExitToShell
               # trap vectors — the port never patches them (static link).
        1036,  # CODE 5+0x5124: _VInstall ($A033) glue — the Falcon VBL
               # service owns the vblank; the Mac VBL task is subsumed.
        1040,  # CODE 5+0x5666: THINK C low-memory vector glue — no Mac
               # low-memory globals exist on the Falcon (the jt9 class).
        1050,  # CODE 5+0x59ee: _KillIO ($A006) glue — every shim IO is
               # synchronous, so there is never a pending op to cancel.
        1052,  # CODE 5+0x5af0: _Eject ($A017) glue — no ejectable media
               # on the GEMDOS C: mount (the flat-dir/GetVol ruling).
        1065,  # CODE 5+0x4a74: the Pack15 ($AC15) selector farm — no
               # Package Manager on the Falcon.
        1110,  # CODE 4+0x79ce: literal `return 3600` (ticks-per-minute).
        1158,  # CODE 4+0x4c48: play-window/palette teardown — HAL-moot
               # (the port's play window IS the HAL screen).
        # band 7 batch 19 — more disasm-verified faithful-empty / moot:
        445,   # CODE 3+0x294e: bare `rts`.
        746,   # CODE 18+0x3fda: empty linkw #0 / unlk / rts.
        748,   # CODE 18+0x405a: empty linkw #0 / unlk / rts.
        771,   # CODE 18+0x48f2: empty linkw #0 / unlk / rts.
        774,   # CODE 18+0x4b5c: empty linkw #0 / unlk / rts.
        785,   # CODE 18+0x4f30: empty linkw #0 / unlk / rts.
        1037,  # CODE 5+0x512e: _VRemove ($A034) glue — the jt1036
               # (_VInstall) counterpart, HAL-moot (Falcon VBL owns
               # the vblank; nothing was ever installed to remove).
        489,   # CODE 3+0x0004: bare `rts` (band 7, disasm-verified).
        905,   # CODE 19+0x5b9c: bare `rts` (band 7, disasm-verified).
        594}   # CODE 15+0x0004: bare `rts` (#151, disasm-verified).

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
    195: "lifted as l4db4 (same address; the design string-table region setup)",
    575: "lifted as l3cd4_c17 (CODE 17+0x3cd4; char-gen proficiency/spell-school "
         "bitfield finalize, wired into jt574 create @24073 + @68107). Reads MISSING "
         "only because the _c17 collision suffix hides it from the auto-aliaser.",
    480: "lifted in str.c (the string-table setter jt480(count, table))",
    591: "lifted as l0ce0_c15 (same address; native->little-endian rec fixup)",
    973: "lifted as l4010 (same address; the GLIB group converter)",
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
    # Address-matched lifts whose header lacks the "CODE seg + 0xOFF" comment
    # the auto-detector keys on, so they need an explicit entry. SAFE to list:
    # each is a SUBSTANTIAL body (>700B) at an offset unique to its segment —
    # not the tiny-stub collision class (l25ce/l46e0) that the auto-detector
    # deliberately refuses. Verified 2026-06-19.
    947: "lifted as l709e (same address; the 39-case dungeon event dispatcher, 4.7KB)",
    999: "lifted as l309c (same address; the GLIB glyph blitter, #104)",
    1003: "lifted as l2856 (same address; the GLIB glyph-blit entry, #104, 3.7KB)",
    945: "lifted as l694e (same address; CODE 20 encounter handler, 3.5KB)",
    902: "lifted as l2f6e (same address; CODE 19 char-sheet helper, ~1KB)",
    917: "lifted as l185e (same address; CODE 12 Training Hall helper, ~0.9KB)",
    1035: "lifted as l50fe (same address; CODE 5 helper, ~1.5KB)",
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
    # port_load_savgame RETIRED 2026-07-01: the heuristic SAVGAMA scan is
    # deleted; the party loads in-game via the faithful Hall Load path
    # (jt918 case 8 -> jt582 -> l143e -> jt579), Hatari-verified.
    ("fill_backdrop",
     "'tuned interior tile' GEN.CTL fill standing in for the faithful "
     "piece-placed gen backdrop. Live under menu_run, jt574, "
     "cg_train_screen, cg_draw_sheet; the Hall paints jt81() over it "
     "every frame (ab8a567). RE the gen piece placement, delete."),
    ("port_draw_play_frame / port_hud_text_clut / port_draw_compass",
     "coarse dungeon-HUD chrome over-blit + text CLUT + compass (the #114 "
     "'jank'); faithful composer is jt304 -> L3fd8 (a few jt1001 FRAME "
     "pieces + jt216/L4430 panels)"),
    ("port_run_encounter / port_play_message",
     "play-loop stand-ins over the faithful CODE 15-20 encounter chain "
     "(#115: l3b0e + CODE-20 L026e + l03f6)."),
    ("port_show_intro",
     "title/credits sequence, trace-matched but not lifted from CODE 22"),
    ("port_frame_load / port_always_load / port_menu_load / port_ui_group_base",
     "GLIB bootstrap wiring; faithful = jt464 + jt997/jt1014 plain-name "
     "loader -> flip the live loader to the FAR pool (groundwork b96a694)"),
    ("l309c_tile",
     "BACK ON THE WALL PATH (jt114): blit-time colour-band rebase "
     "(32/64/96) reproducing the GLIB colour-range allocator's "
     "relocate+remap (jt1069 ncopy) at blit time instead of load time; "
     "faithful = remap pixels at load, blit raw l309c/l2d4e."),
    # Hall Remove/View chrome RETIRED 2026-07-02: case 4 -> the faithful
    # L1060 remove body (jt584 + jt19), case 7 -> the L0f74 change-class
    # skeleton; the cg_draw_sheet/cg_rename/cg_modify_sheet/
    # cg_remove_from_party cluster deleted.
    ("menu_run (+ CODE 22 menu chrome)",
     "main-menu driver mirrors the faithful jt315/jt313 build+chrome; "
     "low-distortion (no traceable Mac path draws per-command bars)."),
    # The dead-stand-in sweep (2026-07-01) deleted the caller-less set:
    # port_menu_bar, menu_draw_plates, port_rest, port_begin_adventure,
    # port_save_game/port_load_game, cg_add_character, jt169_reimpl (+
    # jt169_pick/picker_button_track/picker_cmd_button), port_render_geo_*,
    # port_render_topview, port_*_demo, port_l6234_verify + the
    # FRUA_MAP_DEMO/FRUA_3D_DEMO/FRUA_L6234_VERIFY blocks and `make walk`.
    # git history has the bodies; docs/stub-inventory.md has the full list.
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


def _body_status(body):
    """LIFTED vs STUB for a raw C function body (PROBE/(void)/dbg_log + a
    bare `return const;` = STUB; anything else = a real lift)."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)
    body = re.sub(r"PROBE\s*\([^;]*\)\s*;", "", body)
    body = re.sub(r"\(void\)[^;]*;", "", body)
    body = re.sub(r"dbg_log(_num)?\s*\([^;]*\)\s*;", "", body)
    stmts = [s.strip() for s in re.split(r"[\n;]", body) if s.strip()]
    if not stmts:
        return "STUB"
    if len(stmts) == 1 and re.match(r"^return\s*(-?\d+)?$", stmts[0]):
        return "STUB"
    return "LIFTED"


def classify(n, defs):
    if n in NOOP:
        return "NOOP"
    if n not in defs:
        return "ALIAS" if n in ALIAS_LIFTED else "MISSING"
    st = _body_status(defs[n])
    # A real body, but flagged as a non-faithful port reimplementation.
    if st == "LIFTED" and n in STANDIN:
        return "STANDIN"
    return st


def jt_offsets():
    """JT number -> CODE offset (int) from jumptable.txt."""
    off = {}
    pat = re.compile(r"JT\[\s*(\d+)\]\s+\S+\s+CODE\s+\d+\+0x([0-9a-f]+)")
    if os.path.exists(JUMPTBL):
        for line in open(JUMPTBL, errors="replace"):
            m = pat.search(line)
            if m:
                off[int(m.group(1))] = int(m.group(2), 16)
    return off


def local_alias_candidates():
    """off (int) -> list of (status, context) for every boot.c `lXXXX` def.

    A JT lifted under its CODE-local name `lOFF` is an ALIAS, not MISSING (e.g.
    jt314 == l494e). We index every lXXXX body so an address-matched JT can be
    confirmed — but ONLY together with a segment check (confirmed_alias), since
    low offsets like 0x0004 exist in every segment and would otherwise collide."""
    with open(BOOT_C, errors="replace") as fh:
        src = fh.read()
    out = {}
    hdr = re.compile(r"\bstatic\b[^\n;{]*?\bl([0-9a-f]{3,5})\s*\(", re.M)
    for m in hdr.finditer(src):
        off = int(m.group(1), 16)
        b = src.find("{", m.end())
        s = src.find(";", m.end())
        if b < 0 or (s != -1 and s < b):
            continue                      # forward decl
        depth, j = 0, b
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        body = src[b + 1:j]
        ctx = src[max(0, m.start() - 600):m.end() + 5]   # the comment block + hdr
        out.setdefault(off, []).append((_body_status(body), ctx))
    return out


def confirmed_alias(seg, off, cands, unique_off=None):
    """Return 'LIFTED'/'STUB' if a boot.c `lOFF` is the alias for this JT, by
    EITHER of two collision-safe signals — else None:
      (a) its comment names CODE `seg` AND the 0xOFF (handles colliding
          offsets like 0x0004 that exist in every segment), or
      (b) `off` is unique to a single CODE segment across the whole jump
          table (`unique_off`), so an `lOFF` def cannot belong to anyone else.
    Never resolves an ambiguous offset without one of these, so a genuinely
    MISSING entry is never silently marked done."""
    if seg < 0 or off < 0 or off not in cands:
        return None
    # ONLY trust an alias when the local's comment NAMES its CODE segment AND
    # the 0xOFF — the standard lift-header form ("L494e (CODE 22 + 0x494e)").
    # Pure address-matching is NOT safe: boot.c lXXXX names carry no segment,
    # so a coincidental same-offset leaf in another segment (the char-sheet
    # action stub `l25ce` vs jt893's CODE 19 shop, `l46e0` vs jt894) looks like
    # a match and its trivial body even reads as "lifted". A wrong ALIAS hides
    # a real gap, so we require the explicit segment proof. Lifts with a looser
    # comment stay MISSING until their header is tidied or they're listed in
    # ALIAS_LIFTED (verified).  (unique_off kept for the --aliases diagnostic.)
    seg_re = re.compile(r"CODE[\s_]*0*%d\b" % seg)
    off_re = re.compile(r"0x0*%x\b" % off)
    for status, ctx in cands.get(off, []):
        if seg_re.search(ctx) and off_re.search(ctx):
            return status
    return None


BANDS_DETAIL = 2  # emit the per-entry table for this many leading bands


def main():
    args = sys.argv[1:]
    freq = call_frequency()
    defs = boot_definitions()
    ranked = sorted(freq.items(), key=lambda kv: (-kv[1], kv[0]))

    # Auto-resolve aliases: a JT at CODE seg+0xOFF lifted under its CODE-local
    # name `lOFF` (segment-confirmed) is ALIAS/STUB, not MISSING. This keeps the
    # audit honest without hand-maintaining ALIAS_LIFTED for every same-address
    # alias (jt314 == l494e was a false "MISSING" before this).
    offs = jt_offsets()
    cands = local_alias_candidates()
    segmap_for_alias = parse_jumptable()
    # offsets that exist in exactly one CODE segment (collision-free).
    _by_off = {}
    for n, s in segmap_for_alias.items():
        _by_off.setdefault(offs.get(n), set()).add(s)
    unique_off = {o for o, ss in _by_off.items() if len(ss) == 1}
    auto_alias = {}     # n -> 'ALIAS'/'STUB' resolved via lOFF
    status = {}
    for n, _ in ranked:
        st = classify(n, defs)
        if st == "MISSING":
            c = confirmed_alias(segmap_for_alias.get(n, -1),
                                offs.get(n, -1), cands, unique_off)
            if c == "LIFTED":
                st = "ALIAS"
                auto_alias[n] = "ALIAS"
            elif c == "STUB":
                st = "STUB"
                auto_alias[n] = "STUB"
        status[n] = st

    if "--aliases" in args:
        print(f"auto-resolved address-matched aliases ({len(auto_alias)}):")
        for n in sorted(auto_alias):
            print(f"  jt{n} (CODE {segmap_for_alias.get(n)}+0x{offs.get(n,0):04x})"
                  f" -> l{offs.get(n,0):04x}  [{auto_alias[n]}]")
        return

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
    A(f"entries first (the foundation), in {CHUNK}-entry chunks, until all are")
    A("lifted — then glue the structures together. The chunk table targets")
    A("the work; the **Coverage by CODE segment** table shows _what portion is")
    A("used where_ (which subsystem each pending block belongs to). Regenerate:\n")
    A("```sh\npython3 tools/jt_progress.py\n```\n")
    A("Legend: **LIFTED** real body · **NOOP** faithful empty/constant ")
    A("(done) · **ALIAS** lifted under an lXXXX name (done) · **STUB** ")
    A("one-line placeholder (pending) · **STANDIN** real body but a non-")
    A("faithful port reimplementation (pending) · **MISSING** not in boot.c yet.\n")
    A("ALIAS is now **auto-detected**: a JT at CODE seg+0xOFF lifted under its")
    A("CODE-local name `lOFF` is recognised automatically (segment-verified, so")
    A("cross-segment offset collisions like `l0004` never mis-resolve), so the")
    A("MISSING count no longer over-reports alias-lifted entries. List them with")
    A("`python3 tools/jt_progress.py --aliases`. The hand `ALIAS_LIFTED` map only")
    A("needs the *non*-address aliases (trap-glue→shim, renamed thunks).\n")

    ndist = len(ranked)
    done_all = total["LIFTED"] + total["NOOP"] + total["ALIAS"]
    A(f"**{ndist} distinct JT entries are called.** Overall: "
      f"{done_all} done ({total['LIFTED']} lifted, {total['NOOP']} noop, "
      f"{total['ALIAS']} alias), {total['STUB']} stub, "
      f"{total['STANDIN']} stand-in, {total['MISSING']} missing.\n")

    # per-CHUNK summary (50-entry chunks, user-directed 2026-06-19)
    A(f"## Progress by chunk ({CHUNK} most-called at a time)\n")
    A("Smaller than the old 100-entry bands so each chunk is a targetable")
    A("unit. Rank ranges are absolute (legacy band N == rank "
      "(N-1)*100+1 .. N*100).\n")
    A("| Chunk | Rank | done | lifted | noop/alias | stub | standin | missing |")
    A("|------:|------|-----:|-------:|-----------:|-----:|--------:|--------:|")
    for b in range((ndist + CHUNK - 1) // CHUNK):
        seg = ranked[b * CHUNK:(b + 1) * CHUNK]
        if not seg:
            continue
        t = tally(seg)
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        A(f"| {b + 1} | {b * CHUNK + 1}–{b * CHUNK + len(seg)} | "
          f"**{done}/{len(seg)}** | {t['LIFTED']} | "
          f"{t['NOOP'] + t['ALIAS']} | {t['STUB']} | {t['STANDIN']} | "
          f"{t['MISSING']} |")
    A("")

    # ---- coverage by CODE segment ("what portion is used where") ----
    segmap = parse_jumptable()
    A("## Coverage by CODE segment (what's used where)\n")
    A("Every called JT entry mapped to its Mac CODE segment (from")
    A("`jumptable.txt`). `pending` = stub + standin + missing. The segments")
    A("with the most pending entries are the subsystems with the most work")
    A("left; cross-reference the chunk table to see how load-bearing they are.\n")
    A("| CODE | entries | done | stub | standin | missing | pending | subsystem |")
    A("|-----:|--------:|-----:|-----:|--------:|--------:|--------:|-----------|")
    by_seg = {}
    for n, _ in ranked:
        s = segmap.get(n, -1)
        by_seg.setdefault(s, []).append(n)
    for s in sorted(by_seg, key=lambda s: (s if s >= 0 else 999)):
        ns = by_seg[s]
        t = {"LIFTED": 0, "STUB": 0, "NOOP": 0, "ALIAS": 0,
             "STANDIN": 0, "MISSING": 0}
        for n in ns:
            t[status[n]] += 1
        done = t["LIFTED"] + t["NOOP"] + t["ALIAS"]
        pend = t["STUB"] + t["STANDIN"] + t["MISSING"]
        label = SEGMENT_SUBSYS.get(s, "—") if s >= 0 else "(no jumptable entry)"
        seg_id = f"CODE {s}" if s >= 0 else "?"
        A(f"| {seg_id} | {len(ns)} | {done} | {t['STUB']} | {t['STANDIN']} | "
          f"{t['MISSING']} | **{pend}** | {label} |")
    A("")

    # ---- non-JT local lXXXX PROBE-stubs ----
    lstubs = local_stub_scan()
    A("## Local lXXXX leaf stubs (non-JT PROBE-only helpers)\n")
    A(f"CODE-local helpers still PROBE-only in boot.c ({len(lstubs)} found). "
      "These don't appear in the JT scoreboard above but gate the entries "
      "that call them.\n")
    if lstubs:
        A("> " + "  ".join(f"`{n}`" for n in lstubs))
    else:
        A("_None._")
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

    A("## The pending queue (most-called stubs + stand-ins + missing)\n")
    A("The top-100 are fully lifted, so this lists the highest-frequency")
    A("PENDING entries across ALL ranks — the most load-bearing work left,")
    A("each tagged with its CODE segment (cross-ref the segment table). A note")
    A("from `PENDING_NOTES` explains _why_ it is still open where known.\n")
    PEND_LIMIT = 50
    pend = [(n, c) for n, c in ranked
            if status[n] in ("STUB", "STANDIN", "MISSING")]
    if not pend:
        A("_None — every called entry is lifted._\n")
    else:
        A(f"Top {min(PEND_LIMIT, len(pend))} of {len(pend)} pending "
          f"(stub+standin+missing), by call count:\n")
        for n, c in pend[:PEND_LIMIT]:
            seg = segmap.get(n, -1)
            seg_s = f"CODE {seg}" if seg >= 0 else "?"
            note = PENDING_NOTES.get(n, "") or STANDIN.get(n, "")
            extra = f" — {note}" if note else ""
            A(f"- jt{n} ({c} calls, {seg_s}) — {status[n].lower()}{extra}")
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
