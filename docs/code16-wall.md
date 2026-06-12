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
| jt610 | CODE 16+0x4b48 | combat init: installs jt601 into -24070, fills the handler table (90 stores), tail-calls L49e6 | — |
| L49e6 | CODE 16 local | the other 44 handler-table stores (ids 1–44); true local, no JT export | — |
| jt511 | CODE 13+0x05a6 | installs jt538 into -24070 (in-combat swap) | — |
| jt538 | CODE 14+0x2028 | in-combat target callback (~1.8KB) | — |
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
| 1 | -24062 | jt664 | CODE 16+0x00c6 | — |
| 2 | -24058 | jt695 | CODE 16+0x00ec | — |
| 3 | -24054 | jt620 | CODE 16+0x0122 | — |
| 4 | -24050 | jt650 | CODE 16+0x0158 | — |
| 5 | -24046 | jt674 | CODE 16+0x0188 | — |
| 6 | -24042 | jt699 | CODE 16+0x01a8 | — |
| 7 | -24038 | jt699 | CODE 16+0x01a8 | — |
| 8 | -24034 | jt624 | CODE 16+0x01c8 | — |
| 9 | -24030 | jt655 | CODE 16+0x01e8 | — |
| 10 | -24026 | jt686 | CODE 16+0x0236 | — |
| 11 | -24022 | jt674 | CODE 16+0x0188 | — |
| 12 | -24018 | jt614 | CODE 16+0x030e | — |
| 13 | -24014 | jt643 | CODE 16+0x0468 | — |
| 14 | -24010 | jt676 | CODE 16+0x04ec | — |
| 15 | -24006 | jt708 | CODE 16+0x052a | — |
| 16 | -24002 | jt699 | CODE 16+0x01a8 | — |
| 17 | -23998 | jt699 | CODE 16+0x01a8 | — |
| 18 | -23994 | jt674 | CODE 16+0x0188 | — |
| 19 | -23990 | jt634 | CODE 16+0x058a | — |
| 20 | -23986 | jt666 | CODE 16+0x05aa | — |
| 21 | -23982 | jt698 | CODE 16+0x05fc | — |
| 22 | -23978 | jt674 | CODE 16+0x0188 | — |
| 23 | -23974 | jt623 | CODE 16+0x0756 | — |
| 24 | -23970 | jt654 | CODE 16+0x07b2 | — |
| 25 | -23966 | jt685 | CODE 16+0x07d2 | — |
| 26 | -23962 | jt613 | CODE 16+0x07f2 | — |
| 27 | -23958 | jt642 | CODE 16+0x089a | — |
| 28 | -23954 | jt675 | CODE 16+0x096a | — |
| 29 | -23950 | jt674 | CODE 16+0x0188 | — |
| 30 | -23946 | jt706 | CODE 16+0x09a0 | — |
| 31 | -23942 | jt632 | CODE 16+0x09c0 | — |
| 32 | -23938 | jt663 | CODE 16+0x09e0 | — |
| 33 | -23934 | jt694 | CODE 16+0x0a42 | — |
| 34 | -23930 | jt602 | CODE 16+0x0a62 | — |
| 35 | -23926 | jt619 | CODE 16+0x0da4 | — |
| 36 | -23922 | jt649 | CODE 16+0x0ff6 | — |
| 37 | -23918 | jt682 | CODE 16+0x111e | — |
| 38 | -23914 | jt609 | CODE 16+0x1148 | — |
| 39 | -23910 | jt640 | CODE 16+0x1168 | — |
| 40 | -23906 | jt672 | CODE 16+0x118a | — |
| 41 | -23902 | jt704 | CODE 16+0x1270 | — |
| 42 | -23898 | jt629 | CODE 16+0x155e | — |
| 43 | -23894 | jt687 | CODE 16+0x15ae | — |
| 44 | -23890 | jt660 | CODE 16+0x169e | — |
| 45 | -23886 | jt692 | CODE 16+0x16be | — |
| 46 | -23882 | jt704 | CODE 16+0x1270 | — |
| 47 | -23878 | jt607 | CODE 16+0x16de | — |
| 48 | -23874 | jt652 | CODE 16+0x19aa | — |
| 49 | -23870 | jt623 | CODE 16+0x0756 | — |
| 50 | -23866 | jt706 | CODE 16+0x09a0 | — |
| 51 | -23862 | jt703 | CODE 16+0x1da6 | — |
| 52 | -23858 | jt699 | CODE 16+0x01a8 | — |
| 53 | -23854 | jt699 | CODE 16+0x01a8 | — |
| 54 | -23850 | jt699 | CODE 16+0x01a8 | — |
| 55 | -23846 | jt628 | CODE 16+0x1dfa | — |
| 56 | -23842 | jt659 | CODE 16+0x1e1a | — |
| 57 | -23838 | jt699 | CODE 16+0x01a8 | — |
| 58 | -23834 | jt617 | CODE 16+0x1ee2 | — |
| 59 | -23830 | jt685 | CODE 16+0x07d2 | — |
| 60 | -23826 | jt674 | CODE 16+0x0188 | — |
| 61 | -23822 | jt687 | CODE 16+0x15ae | — |
| 62 | -23818 | jt664 | CODE 16+0x00c6 | — |
| 63 | -23814 | jt686 | CODE 16+0x0236 | — |
| 64 | -23810 | jt655 | CODE 16+0x01e8 | — |
| 66 | -23802 | jt627 | CODE 16+0x20b6 | — |
| 67 | -23798 | jt658 | CODE 16+0x20e8 | — |
| 68 | -23794 | jt690 | CODE 16+0x21c8 | — |
| 69 | -23790 | jt699 | CODE 16+0x01a8 | — |
| 70 | -23786 | jt616 | CODE 16+0x2242 | — |
| 71 | -23782 | jt646 | CODE 16+0x22e8 | — |
| 72 | -23778 | jt679 | CODE 16+0x2320 | — |
| 73 | -23774 | jt605 | CODE 16+0x2352 | — |
| 74 | -23770 | jt636 | CODE 16+0x239c | — |
| 75 | -23766 | jt668 | CODE 16+0x23ea | — |
| 76 | -23762 | jt701 | CODE 16+0x24a0 | — |
| 77 | -23758 | jt674 | CODE 16+0x0188 | — |
| 78 | -23754 | jt626 | CODE 16+0x2552 | — |
| 79 | -23750 | jt657 | CODE 16+0x2604 | — |
| 80 | -23746 | jt689 | CODE 16+0x2614 | — |
| 81 | -23742 | jt615 | CODE 16+0x2634 | — |
| 82 | -23738 | jt645 | CODE 16+0x26b2 | — |
| 83 | -23734 | jt678 | CODE 16+0x2776 | — |
| 84 | -23730 | jt603 | CODE 16+0x2806 | — |
| 85 | -23726 | jt635 | CODE 16+0x2960 | — |
| 86 | -23722 | jt667 | CODE 16+0x2ab8 | — |
| 87 | -23718 | jt700 | CODE 16+0x2b90 | — |
| 88 | -23714 | jt625 | CODE 16+0x2c00 | — |
| 89 | -23710 | jt687 | CODE 16+0x15ae | — |
| 90 | -23706 | jt656 | CODE 16+0x2c20 | — |
| 91 | -23702 | jt602 | CODE 16+0x0a62 | — |
| 92 | -23698 | jt688 | CODE 16+0x2c40 | — |
| 93 | -23694 | jt696 | CODE 16+0x2d18 | — |
| 94 | -23690 | jt623 | CODE 16+0x0756 | — |
| 96 | -23682 | jt651 | CODE 16+0x2e28 | — |
| 98 | -23674 | jt611 | CODE 16+0x2f2e | — |
| 100 | -23666 | jt671 | CODE 16+0x2f9e | — |
| 101 | -23662 | jt602 | CODE 16+0x0a62 | — |
| 102 | -23658 | jt697 | CODE 16+0x2fbe | — |
| 103 | -23654 | jt618 | CODE 16+0x3218 | — |
| 104 | -23650 | jt644 | CODE 16+0x323c | — |
| 105 | -23646 | jt673 | CODE 16+0x32ae | — |
| 106 | -23642 | jt640 | CODE 16+0x1168 | — |
| 107 | -23638 | jt658 | CODE 16+0x20e8 | — |
| 108 | -23634 | jt705 | CODE 16+0x3336 | — |
| 109 | -23630 | jt622 | CODE 16+0x3384 | — |
| 110 | -23626 | jt648 | CODE 16+0x3424 | — |
| 111 | -23622 | jt677 | CODE 16+0x355e | — |
| 112 | -23618 | jt707 | CODE 16+0x3614 | — |
| 113 | -23614 | jt630 | CODE 16+0x3634 | — |
| 114 | -23610 | jt653 | CODE 16+0x37bc | — |
| 115 | -23606 | jt607 | CODE 16+0x16de | — |
| 116 | -23602 | jt681 | CODE 16+0x381a | — |
| 117 | -23598 | jt604 | CODE 16+0x38d6 | — |
| 118 | -23594 | jt633 | CODE 16+0x3a4c | — |
| 119 | -23590 | jt662 | CODE 16+0x3a84 | — |
| 120 | -23586 | jt684 | CODE 16+0x3aa4 | — |
| 121 | -23582 | jt608 | CODE 16+0x3c38 | — |
| 122 | -23578 | jt699 | CODE 16+0x01a8 | — |
| 123 | -23574 | jt665 | CODE 16+0x3d02 | — |
| 124 | -23570 | jt693 | CODE 16+0x3f8a | — |
| 125 | -23566 | jt612 | CODE 16+0x4338 | — |
| 126 | -23562 | jt639 | CODE 16+0x4458 | — |
| 127 | -23558 | jt691 | CODE 16+0x1eb0 | — |
| 128 | -23554 | jt647 | CODE 16+0x1f1a | — |
| 129 | -23550 | jt680 | CODE 16+0x1f96 | — |
| 130 | -23546 | jt606 | CODE 16+0x1fae | — |
| 131 | -23542 | jt637 | CODE 16+0x1fce | — |
| 132 | -23538 | jt669 | CODE 16+0x2008 | — |
| 133 | -23534 | jt607 | CODE 16+0x16de | — |
| 134 | -23530 | jt702 | CODE 16+0x2084 | — |
| 135 | -23526 | jt621 | CODE 16+0x2e08 | — |
| 136 | -23522 | jt683 | CODE 16+0x2f0e | — |
| 137 | -23518 | jt641 | CODE 16+0x2f64 | — |

### Unique handlers (102)

All currently missing:
- MISSING (102): jt602 jt603 jt604 jt605 jt606 jt607 jt608 jt609 jt611 jt612 jt613 jt614 jt615 jt616 jt617 jt618 jt619 jt620 jt621 jt622 jt623 jt624 jt625 jt626 jt627 jt628 jt629 jt630 jt632 jt633 jt634 jt635 jt636 jt637 jt639 jt640 jt641 jt642 jt643 jt644 jt645 jt646 jt647 jt648 jt649 jt650 jt651 jt652 jt653 jt654 jt655 jt656 jt657 jt658 jt659 jt660 jt662 jt663 jt664 jt665 jt666 jt667 jt668 jt669 jt671 jt672 jt673 jt674 jt675 jt676 jt677 jt678 jt679 jt680 jt681 jt682 jt683 jt684 jt685 jt686 jt687 jt688 jt689 jt690 jt691 jt692 jt693 jt694 jt695 jt696 jt697 jt698 jt699 jt700 jt701 jt702 jt703 jt704 jt705 jt706 jt707 jt708

## 3. Worklist discipline

1. Pick a handler (or wall piece) above.
2. `git grep -n 'static.*\bjtNNN(' src/engine/boot.c` — the duplicate
   check — plus the dual-name check against
   `data/work/disasm/jumptable.txt` for every local the disasm names.
3. Lift, gate (`make`, codegen grep, `make -s test`), update this
   file's status column, commit both together.
