# CODE 16 wall — combat registration + spell-effect handler tier

**The running worklist for making the CODE 18 handler family LIVE.**
Update the status columns in the same commit as each lift — this file is
the duplicate-work check: consult it (and `git grep 'static.*\bjtNNN('
src/engine/boot.c`) BEFORE lifting anything listed here.

Status legend: **LIFTED** real body / `stub` PROBE placeholder in
boot.c / `—` missing entirely.  Cross-check any row with
`python3 tools/seg_audit.py 16` (or 13/14 for the off-segment pieces).

## 1. The wall itself (registration + entry points)

Everything the jt7xx special-attack handlers and the jt598 effect
dispatch need before they fire for real:

| piece | where | what | status |
|---|---|---|---|
| jt601 | CODE 16+0x63ae | out-of-combat target callback (-24070) | **LIFTED** |
| jt888 | CODE 19+0x596a | "Cast Spell on whom" interactive picker | **LIFTED** |
| jt598 | CODE 16+0x7280 | effect dispatcher over the -24066 table | **LIFTED** |
| jt856 | CODE 18+0x725c | 230-slot special-attack hook table (-25242) | **LIFTED** (3965c7f) |
| jt610 | CODE 16+0x4b48 | combat init: installs jt601 into -24070, fills the handler table (90 stores), tail-calls L49e6 | **LIFTED** |
| L49e6 | CODE 16 local | the other 44 handler-table stores (ids 1–44); true local, no JT export | **LIFTED** (l49e6) |
| jt511 | CODE 13+0x05a6 | the COMBAT MAIN LOOP; installs jt538 into -24070 at entry | **LIFTED** (level 2 — heavy locals stubbed, below) |
| jt538 | CODE 14+0x2028 | in-combat target callback (~1.8KB) | **LIFTED** |
| L1efa | CODE 14 local | one target pick for jt538 (crosshair / list / area re-aim) | **LIFTED** (l1efa) |
| jt600 | CODE 16+0x5f26 | spell range from the -16906 row (base + per-level) | **LIFTED** |
| jt539 | CODE 14+0x3b6c | interactive in-combat crosshair pick UI | stub |
| L1dd6 | CODE 14 local (~292B) | repeat pick from the built area list | stub (l1dd6) |
| L4dee | CODE 14 local | repeat pick with per-target area re-aim (jt508) | stub (l4dee) |
| L4d98 | CODE 6+0x4d98 | the GAME-SESSION INIT — sole caller of jt610 + jt856; **REGISTRATION IS LIVE** (runs in ua_main at boot) | **LIFTED** (l4d98; resident-palette arm + jt151 PORT-DEFERRED, see code) |
| jt86 / jt87 | CODE 6+0x4c96 / +0x5252 | ITEMS.DAT / item.dat load callbacks (item template + record tables) | **LIFTED** |
| jt996 / jt1000 / jt1186 / jt1201 | CODE 5/4 | TPalette raw CLUT commit family (jt1186 port-maps to l6e58) | **LIFTED** (callers deferred w/ palette arm) |
| jt986 / jt975 / jt964 / L3736 / L7eb8 | CODE 5 | sound-bank load (music.slb) + sample sign-flip ("sounds" GLIB 18) | **LIFTED** |
| jt974 | CODE 5+0x1304 (~600B) | the sound-mixer pump installed at -4774 | stub |
| jt137 | CODE 7+0x1234 (~830B) | the COMMAND-BUTTON DLItem method (bar from FRAME glyphs 10-15, label + hotkey letter via jt1089, scaled hit rect; chains other msgs to the saved jt382) | **LIFTED** — jt151 GATED on the (v,h) coordinate migration, see docs/coord-audit.md |
| jt900/jt1115/jt149/jt151/jt450/jt1086/jt1090/jt1166/jt1179/jt976/jt978 | misc | small session-init leaves | **LIFTED** |
| L59d6/L31cc/L3cb2/L36e0/L3fb2/L5304 | CODE 6 locals | audio bring-up / name copy / binder reset+claim / audio tail / item.dat | **LIFTED** |
| jt50/jt51/jt64 | CODE 6 | = the ALREADY-LIFTED l5ac2/l5ad8/l5f3a (dual-name trap caught in audit); JT names route there | **LIFTED** |
| L0006 | CODE 13 (+0x010e site) | combat teardown; reinstalls jt601 | **LIFTED** (l0006) |
| L076e | CODE 13 local (~2.2KB) | execute one actor's combat turn (the heart of the loop) | **LIFTED** (l076e) — spine now wired l159a→jt511→l076e→l08b4/l5008 |
| L602c | CODE 16+0x602c (~220B) | effect DURATION/magnitude in rounds (JT[1] switch over spell id; default = base + jt17·per-level from -16906) | **LIFTED** (l602c) — first CODE-16 card |
| L6114 | CODE 16+0x6114 (~660B) | the effect-APPLICATION core: walk the -23512 target list, per target apply effect `code` w/ magic-resist (jt866) + saves (jt864/jt867/jt871) + announce | stub — **NEXT card** (deps all lifted incl. l602c) |
| L0434 | CODE 13 local | per-round init: builds the -22624 initiative slots | stub (l0434) |
| L102a | CODE 13 local | end-of-round bookkeeping; may end the fight | stub (l102a) |
| L4f22 | CODE 13 local | combat entry staging | stub (l4f22) |
| L0116 | CODE 13 local | post-combat aftermath (when -27916 set) | stub (l0116) |
| jt542 | CODE 14+0x5434 | combat-field setup at loop entry | stub |
| jt541 | CODE 14+0x0006 | per-member per-round prep | stub |
| jt50/jt51/jt64 | CODE 6 | combat-loop keyboard handlers (338/339/Esc) | stub |
| jt67 | CODE 6+0x5f48 | combat-abort poll | stub |
| jt55/jt58 | CODE 6 | art/resource teardown leaves (l0006) | stub |
~~l510c_c6~~ — 0x510c turned out to be the jt856 CALL SITE inside L4d98, not a function; row retired.

**Port bugs found by wiring L4d98 live (all fixed in the same commit):**
- `JT465_RECORD_MAX` was 64; the Mac table is 48 (freemap size). The
  oversized jt463 zero-fill wiped the DLItem shape-method table (-9282),
  pool base (-9286) and dialog state — killing every menu label + key.
- `jt1182` direction was inverted (jt406's Mac ABI is copy(src, dst, n) —
  CODE 3 L57f8's first arg is the SOURCE); it now loads the -17498
  template INTO -3016. **Audit the other jt406 lifts for the same
  positional inversion** (jt993/jt1066/jt1067 verified correct).
- The GLIB FAR pool was never opened in the live boot (only the probe
  self-test); `glib_pool_open()` now runs in master_init at the Mac's
  jt1079 call site, with the Mac-style negotiate-down sizing (4MB safe).
- `g_a5_2347` (colour-mode flag) was unseeded at boot, so jt1200()==3
  selected the B&W .TLB libraries; seeded to 1 in boot_a5_seed_defaults.
- l4cc0 (design buffers) now runs at the Mac's spot (before L4d98) and
  is idempotent; its -27944/-27920 item-table allocs are un-deferred
  (jt86/jt87 are their consumers).
| jt599 | CODE 16+0x64a8 | cast/apply one effect by id (~1.25KB; the beholder self-casts through it) | **LIFTED** |
| jt546 | CODE 14+0x4186 | combat target picker (~570B; jt537 inert until lifted) | stub |

jt610's flag side-effects at entry: clear -25258 and -23230, set
-25256 = 1.

## 2. The -24066 spell-effect handler table

`jt598(code)` calls `*(long *)(A5 - 24066 + code*4)` with NO args
(`void (*)(void)` ABI — unlike the -25242 hook table's
`(rec, node, flag)`).  133 installs over effect ids 1–137 (gaps stay
NULL — id 0 is never installed); jt610 covers ids 45–137, L49e6 ids
1–44.  **102 unique handlers, all CODE 16.**  jt699 (6 installs) and
the other repeats are shared/generic handlers.

Known anchors: id 10 = jt686 is the gaze effect (jt727 calls
`jt598(10)`); the beholder's jt599 self-casts land on ids 84 = jt603,
55 = jt628, 21 = jt698.

| id | A5 slot | handler | CODE addr | status |
|---:|---:|---|---|---|
| 1 | -24062 | jt664 | CODE 16+0x00c6 | stub |
| 2 | -24058 | jt695 | CODE 16+0x00ec | stub |
| 3 | -24054 | jt620 | CODE 16+0x0122 | stub |
| 4 | -24050 | jt650 | CODE 16+0x0158 | stub |
| 5 | -24046 | jt674 | CODE 16+0x0188 | LIFTED |
| 6 | -24042 | jt699 | CODE 16+0x01a8 | stub |
| 7 | -24038 | jt699 | CODE 16+0x01a8 | stub |
| 8 | -24034 | jt624 | CODE 16+0x01c8 | stub |
| 9 | -24030 | jt655 | CODE 16+0x01e8 | stub |
| 10 | -24026 | jt686 | CODE 16+0x0236 | stub |
| 11 | -24022 | jt674 | CODE 16+0x0188 | LIFTED |
| 12 | -24018 | jt614 | CODE 16+0x030e | stub |
| 13 | -24014 | jt643 | CODE 16+0x0468 | stub |
| 14 | -24010 | jt676 | CODE 16+0x04ec | stub |
| 15 | -24006 | jt708 | CODE 16+0x052a | stub |
| 16 | -24002 | jt699 | CODE 16+0x01a8 | stub |
| 17 | -23998 | jt699 | CODE 16+0x01a8 | stub |
| 18 | -23994 | jt674 | CODE 16+0x0188 | LIFTED |
| 19 | -23990 | jt634 | CODE 16+0x058a | stub |
| 20 | -23986 | jt666 | CODE 16+0x05aa | stub |
| 21 | -23982 | jt698 | CODE 16+0x05fc | stub |
| 22 | -23978 | jt674 | CODE 16+0x0188 | LIFTED |
| 23 | -23974 | jt623 | CODE 16+0x0756 | stub |
| 24 | -23970 | jt654 | CODE 16+0x07b2 | stub |
| 25 | -23966 | jt685 | CODE 16+0x07d2 | stub |
| 26 | -23962 | jt613 | CODE 16+0x07f2 | stub |
| 27 | -23958 | jt642 | CODE 16+0x089a | stub |
| 28 | -23954 | jt675 | CODE 16+0x096a | stub |
| 29 | -23950 | jt674 | CODE 16+0x0188 | LIFTED |
| 30 | -23946 | jt706 | CODE 16+0x09a0 | stub |
| 31 | -23942 | jt632 | CODE 16+0x09c0 | stub |
| 32 | -23938 | jt663 | CODE 16+0x09e0 | stub |
| 33 | -23934 | jt694 | CODE 16+0x0a42 | stub |
| 34 | -23930 | jt602 | CODE 16+0x0a62 | stub |
| 35 | -23926 | jt619 | CODE 16+0x0da4 | stub |
| 36 | -23922 | jt649 | CODE 16+0x0ff6 | stub |
| 37 | -23918 | jt682 | CODE 16+0x111e | stub |
| 38 | -23914 | jt609 | CODE 16+0x1148 | stub |
| 39 | -23910 | jt640 | CODE 16+0x1168 | stub |
| 40 | -23906 | jt672 | CODE 16+0x118a | stub |
| 41 | -23902 | jt704 | CODE 16+0x1270 | stub |
| 42 | -23898 | jt629 | CODE 16+0x155e | stub |
| 43 | -23894 | jt687 | CODE 16+0x15ae | LIFTED |
| 44 | -23890 | jt660 | CODE 16+0x169e | stub |
| 45 | -23886 | jt692 | CODE 16+0x16be | stub |
| 46 | -23882 | jt704 | CODE 16+0x1270 | stub |
| 47 | -23878 | jt607 | CODE 16+0x16de | stub |
| 48 | -23874 | jt652 | CODE 16+0x19aa | stub |
| 49 | -23870 | jt623 | CODE 16+0x0756 | stub |
| 50 | -23866 | jt706 | CODE 16+0x09a0 | stub |
| 51 | -23862 | jt703 | CODE 16+0x1da6 | stub |
| 52 | -23858 | jt699 | CODE 16+0x01a8 | stub |
| 53 | -23854 | jt699 | CODE 16+0x01a8 | stub |
| 54 | -23850 | jt699 | CODE 16+0x01a8 | stub |
| 55 | -23846 | jt628 | CODE 16+0x1dfa | stub |
| 56 | -23842 | jt659 | CODE 16+0x1e1a | stub |
| 57 | -23838 | jt699 | CODE 16+0x01a8 | stub |
| 58 | -23834 | jt617 | CODE 16+0x1ee2 | stub |
| 59 | -23830 | jt685 | CODE 16+0x07d2 | stub |
| 60 | -23826 | jt674 | CODE 16+0x0188 | LIFTED |
| 61 | -23822 | jt687 | CODE 16+0x15ae | LIFTED |
| 62 | -23818 | jt664 | CODE 16+0x00c6 | stub |
| 63 | -23814 | jt686 | CODE 16+0x0236 | stub |
| 64 | -23810 | jt655 | CODE 16+0x01e8 | stub |
| 66 | -23802 | jt627 | CODE 16+0x20b6 | stub |
| 67 | -23798 | jt658 | CODE 16+0x20e8 | stub |
| 68 | -23794 | jt690 | CODE 16+0x21c8 | stub |
| 69 | -23790 | jt699 | CODE 16+0x01a8 | stub |
| 70 | -23786 | jt616 | CODE 16+0x2242 | stub |
| 71 | -23782 | jt646 | CODE 16+0x22e8 | stub |
| 72 | -23778 | jt679 | CODE 16+0x2320 | stub |
| 73 | -23774 | jt605 | CODE 16+0x2352 | stub |
| 74 | -23770 | jt636 | CODE 16+0x239c | stub |
| 75 | -23766 | jt668 | CODE 16+0x23ea | stub |
| 76 | -23762 | jt701 | CODE 16+0x24a0 | stub |
| 77 | -23758 | jt674 | CODE 16+0x0188 | LIFTED |
| 78 | -23754 | jt626 | CODE 16+0x2552 | stub |
| 79 | -23750 | jt657 | CODE 16+0x2604 | stub |
| 80 | -23746 | jt689 | CODE 16+0x2614 | stub |
| 81 | -23742 | jt615 | CODE 16+0x2634 | stub |
| 82 | -23738 | jt645 | CODE 16+0x26b2 | stub |
| 83 | -23734 | jt678 | CODE 16+0x2776 | stub |
| 84 | -23730 | jt603 | CODE 16+0x2806 | stub |
| 85 | -23726 | jt635 | CODE 16+0x2960 | stub |
| 86 | -23722 | jt667 | CODE 16+0x2ab8 | stub |
| 87 | -23718 | jt700 | CODE 16+0x2b90 | stub |
| 88 | -23714 | jt625 | CODE 16+0x2c00 | stub |
| 89 | -23710 | jt687 | CODE 16+0x15ae | LIFTED |
| 90 | -23706 | jt656 | CODE 16+0x2c20 | stub |
| 91 | -23702 | jt602 | CODE 16+0x0a62 | stub |
| 92 | -23698 | jt688 | CODE 16+0x2c40 | stub |
| 93 | -23694 | jt696 | CODE 16+0x2d18 | stub |
| 94 | -23690 | jt623 | CODE 16+0x0756 | stub |
| 96 | -23682 | jt651 | CODE 16+0x2e28 | stub |
| 98 | -23674 | jt611 | CODE 16+0x2f2e | stub |
| 100 | -23666 | jt671 | CODE 16+0x2f9e | stub |
| 101 | -23662 | jt602 | CODE 16+0x0a62 | stub |
| 102 | -23658 | jt697 | CODE 16+0x2fbe | stub |
| 103 | -23654 | jt618 | CODE 16+0x3218 | stub |
| 104 | -23650 | jt644 | CODE 16+0x323c | stub |
| 105 | -23646 | jt673 | CODE 16+0x32ae | stub |
| 106 | -23642 | jt640 | CODE 16+0x1168 | stub |
| 107 | -23638 | jt658 | CODE 16+0x20e8 | stub |
| 108 | -23634 | jt705 | CODE 16+0x3336 | stub |
| 109 | -23630 | jt622 | CODE 16+0x3384 | stub |
| 110 | -23626 | jt648 | CODE 16+0x3424 | stub |
| 111 | -23622 | jt677 | CODE 16+0x355e | stub |
| 112 | -23618 | jt707 | CODE 16+0x3614 | stub |
| 113 | -23614 | jt630 | CODE 16+0x3634 | stub |
| 114 | -23610 | jt653 | CODE 16+0x37bc | stub |
| 115 | -23606 | jt607 | CODE 16+0x16de | stub |
| 116 | -23602 | jt681 | CODE 16+0x381a | stub |
| 117 | -23598 | jt604 | CODE 16+0x38d6 | stub |
| 118 | -23594 | jt633 | CODE 16+0x3a4c | stub |
| 119 | -23590 | jt662 | CODE 16+0x3a84 | stub |
| 120 | -23586 | jt684 | CODE 16+0x3aa4 | stub |
| 121 | -23582 | jt608 | CODE 16+0x3c38 | stub |
| 122 | -23578 | jt699 | CODE 16+0x01a8 | stub |
| 123 | -23574 | jt665 | CODE 16+0x3d02 | stub |
| 124 | -23570 | jt693 | CODE 16+0x3f8a | stub |
| 125 | -23566 | jt612 | CODE 16+0x4338 | stub |
| 126 | -23562 | jt639 | CODE 16+0x4458 | stub |
| 127 | -23558 | jt691 | CODE 16+0x1eb0 | stub |
| 128 | -23554 | jt647 | CODE 16+0x1f1a | stub |
| 129 | -23550 | jt680 | CODE 16+0x1f96 | stub |
| 130 | -23546 | jt606 | CODE 16+0x1fae | stub |
| 131 | -23542 | jt637 | CODE 16+0x1fce | stub |
| 132 | -23538 | jt669 | CODE 16+0x2008 | stub |
| 133 | -23534 | jt607 | CODE 16+0x16de | stub |
| 134 | -23530 | jt702 | CODE 16+0x2084 | stub |
| 135 | -23526 | jt621 | CODE 16+0x2e08 | stub |
| 136 | -23522 | jt683 | CODE 16+0x2f0e | stub |
| 137 | -23518 | jt641 | CODE 16+0x2f64 | stub |

### Unique handlers — table LIVE-shaped; each is its own lift

The simplest family is the **status-announce** handler: it loads -25262 (the
current target id) and a message string and calls `l6114(target,0,0,0,0,msg)` —
nothing else. jt699 ("is protected") was the template; 16 more lifted 2026-06-15
(jt606/609/624/625/632/634/654/656/660/662/685/689/692/694/706/707). The rest
have real per-effect bodies (dice rolls, target tables, jt521 burst render).

- LIFTED (25): jt602 jt606 jt607 jt609 jt610 jt623 jt624 jt625 jt632 jt634
  jt638 jt654 jt656 jt660 jt661 jt662 jt674 jt685 jt687 jt689 jt692 jt694 jt699
  jt706 jt707
- STUB (81): jt603 jt604 jt605 jt608 jt611 jt612 jt613 jt614 jt615 jt616 jt617
  jt618 jt619 jt620 jt621 jt622 jt626 jt627 jt628 jt629 jt630 jt631 jt633 jt635
  jt636 jt637 jt639 jt640 jt641 jt642 jt643 jt644 jt645 jt646 jt647 jt648 jt649
  jt650 jt651 jt652 jt653 jt655 jt657 jt658 jt659 jt663 jt664 jt665 jt666 jt667
  jt668 jt669 jt671 jt672 jt673 jt675 jt676 jt677 jt678 jt679 jt680 jt681 jt682
  jt683 jt684 jt686 jt688 jt690 jt691 jt693 jt695 jt696 jt697 jt698 jt700 jt701
  jt702 jt703 jt704 jt705 jt708

The combat spine is now LIFTED (l076e + jt511 + l08b4/l5008, 2026-06-24), so the
handler table is reachable in principle — but the handlers remain **breadth-first,
runtime-untested**, and most funnel through **l6114** (the effect-application
core), which is still a stub. Lifting l6114 (next card; its leaf l602c is now
done) is what makes the announce family actually produce output.

## 3. Worklist discipline

1. Pick a handler (or wall piece) above.
2. `git grep -n 'static.*\bjtNNN(' src/engine/boot.c` — the duplicate
   check — plus the dual-name check against
   `data/work/disasm/jumptable.txt` for every local the disasm names.
3. Lift, gate (`make`, codegen grep, `make -s test`), update this
   file's status column, commit both together.
