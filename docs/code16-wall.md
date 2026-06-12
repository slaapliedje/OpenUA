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
| jt511 | CODE 13+0x05a6 | installs jt538 into -24070 (in-combat swap) | — |
| jt538 | CODE 14+0x2028 | in-combat target callback (~1.8KB) | **LIFTED** |
| L1efa | CODE 14 local | one target pick for jt538 (crosshair / list / area re-aim) | **LIFTED** (l1efa) |
| jt600 | CODE 16+0x5f26 | spell range from the -16906 row (base + per-level) | **LIFTED** |
| jt539 | CODE 14+0x3b6c | interactive in-combat crosshair pick UI | stub |
| L1dd6 | CODE 14 local (~292B) | repeat pick from the built area list | stub (l1dd6) |
| L4dee | CODE 14 local | repeat pick with per-target area re-aim (jt508) | stub (l4dee) |
| L0006 | CODE 13 (+0x010e site) | combat teardown; reinstalls jt601 | — |
| l510c_c6 | CODE 6+0x510c | combat setup; sole caller of jt856 | — |
| jt599 | CODE 16+0x64a8 | cast/apply one effect by id (~1.25KB; the beholder self-casts through it) | stub |
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
| 5 | -24046 | jt674 | CODE 16+0x0188 | stub |
| 6 | -24042 | jt699 | CODE 16+0x01a8 | stub |
| 7 | -24038 | jt699 | CODE 16+0x01a8 | stub |
| 8 | -24034 | jt624 | CODE 16+0x01c8 | stub |
| 9 | -24030 | jt655 | CODE 16+0x01e8 | stub |
| 10 | -24026 | jt686 | CODE 16+0x0236 | stub |
| 11 | -24022 | jt674 | CODE 16+0x0188 | stub |
| 12 | -24018 | jt614 | CODE 16+0x030e | stub |
| 13 | -24014 | jt643 | CODE 16+0x0468 | stub |
| 14 | -24010 | jt676 | CODE 16+0x04ec | stub |
| 15 | -24006 | jt708 | CODE 16+0x052a | stub |
| 16 | -24002 | jt699 | CODE 16+0x01a8 | stub |
| 17 | -23998 | jt699 | CODE 16+0x01a8 | stub |
| 18 | -23994 | jt674 | CODE 16+0x0188 | stub |
| 19 | -23990 | jt634 | CODE 16+0x058a | stub |
| 20 | -23986 | jt666 | CODE 16+0x05aa | stub |
| 21 | -23982 | jt698 | CODE 16+0x05fc | stub |
| 22 | -23978 | jt674 | CODE 16+0x0188 | stub |
| 23 | -23974 | jt623 | CODE 16+0x0756 | stub |
| 24 | -23970 | jt654 | CODE 16+0x07b2 | stub |
| 25 | -23966 | jt685 | CODE 16+0x07d2 | stub |
| 26 | -23962 | jt613 | CODE 16+0x07f2 | stub |
| 27 | -23958 | jt642 | CODE 16+0x089a | stub |
| 28 | -23954 | jt675 | CODE 16+0x096a | stub |
| 29 | -23950 | jt674 | CODE 16+0x0188 | stub |
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
| 43 | -23894 | jt687 | CODE 16+0x15ae | stub |
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
| 60 | -23826 | jt674 | CODE 16+0x0188 | stub |
| 61 | -23822 | jt687 | CODE 16+0x15ae | stub |
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
| 77 | -23758 | jt674 | CODE 16+0x0188 | stub |
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
| 89 | -23710 | jt687 | CODE 16+0x15ae | stub |
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

### Unique handlers (102)

All PROBE-stubbed (table is LIVE-shaped; each stub is its own lift):
- STUB (102): jt602 jt603 jt604 jt605 jt606 jt607 jt608 jt609 jt611 jt612 jt613 jt614 jt615 jt616 jt617 jt618 jt619 jt620 jt621 jt622 jt623 jt624 jt625 jt626 jt627 jt628 jt629 jt630 jt632 jt633 jt634 jt635 jt636 jt637 jt639 jt640 jt641 jt642 jt643 jt644 jt645 jt646 jt647 jt648 jt649 jt650 jt651 jt652 jt653 jt654 jt655 jt656 jt657 jt658 jt659 jt660 jt662 jt663 jt664 jt665 jt666 jt667 jt668 jt669 jt671 jt672 jt673 jt674 jt675 jt676 jt677 jt678 jt679 jt680 jt681 jt682 jt683 jt684 jt685 jt686 jt687 jt688 jt689 jt690 jt691 jt692 jt693 jt694 jt695 jt696 jt697 jt698 jt699 jt700 jt701 jt702 jt703 jt704 jt705 jt706 jt707 jt708

## 3. Worklist discipline

1. Pick a handler (or wall piece) above.
2. `git grep -n 'static.*\bjtNNN(' src/engine/boot.c` — the duplicate
   check — plus the dual-name check against
   `data/work/disasm/jumptable.txt` for every local the disasm names.
3. Lift, gate (`make`, codegen grep, `make -s test`), update this
   file's status column, commit both together.
