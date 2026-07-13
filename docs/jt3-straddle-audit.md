# JT[3] straddle audit — arms lifted from a garbled listing

Until 2026-07-13, `tools/dis68k.py` never resynced after a JT[3] inline
switch table: a garbage "instruction" straddling the table's end ate the
first real code bytes after it, so **the first instruction(s) of a case arm
listed as a stray `.short`** (and its `LXXXX:` label was missing). The
canonical victim was jt433 — its form-feed arm lost `jsr L4854`
(PrClosePage) and was mis-lifted; the truth had to be recovered from raw
bytes (docs/gdos-printing-wall.md).

The resync fix (`resync_stream`, commit a70368d) repaired **145 sites**.
Every lift made from the OLD listing of one of these sites is suspect in
exactly the jt433 way: the arm may be missing its first call/assignment.

## Method (per site)

1. Open the CURRENT listing at the site — the repaired instruction(s) sit
   under a now-present `LXXXX:` label right after a JT[3] table block.
2. Find that arm in the lifted C (the owner function below, or the local
   helper the site actually falls in — attribution is "nearest preceding
   entry_jtN", so a site may belong to an lXXXX helper after that export).
3. Confirm the C arm's first statements match the repaired asm. Fix + smoke
   if not, then tick the site here.

Owners marked PROBE stub had nothing lifted from the bad text — skip.

## Sites by enclosing export

| owner | status | sites |
|---|---|---|
| (pre-export) | CODE 17 pre-export helpers (char-gen tables region) | [x] CODE 17+0x01a8, [x] CODE 17+0x02fc, [x] CODE 17+0x035c, [x] CODE 17+0x03bc, [x] CODE 17+0x042e, [x] CODE 17+0x06c2, [x] CODE 17+0x0722, [x] CODE 22+0x00da |
| jt17 | ✅ CLEAN 2026-07-13 | [x] CODE 6+0x27aa |
| jt21 | ✅ CLEAN 2026-07-13 | [x] CODE 6+0x1790 |
| jt131 | ✅ CLEAN 2026-07-13 | [x] CODE 6+0x038a |
| jt183 | ✅ GAP CLOSED 2026-07-13 — case 0 (the shop BUY action) calls L3c7c, which had never been lifted. L3c7c + l38fe + l3bcc now lifted and live-verified. | [x] CODE 7+0x4024 |
| jt207 | 🔴 MIS-LIFT FIXED — l5484 (JT[207]) case 0 and default were SWAPPED: style 0 must return 1, out-of-range returns 0. Area-map edge classifier 2026-07-13 | [x] CODE 7+0x54e0 |
| jt224 | ✅ CLEAN 2026-07-13 | [x] CODE 7+0x0898 |
| jt225 | ✅ CLEAN 2026-07-13 | [x] CODE 7+0x0bbe |
| jt233 | ✅ AUDITED 2026-07-13 — 1 MIS-LIFT FIXED: case 1 (wall type) was missing `ret = 11` (0x0320 moveq #11, the eaten instruction); 0x040a/0x045a clean | [x] CODE 11+0x0320, [x] CODE 11+0x040a, [x] CODE 11+0x045a |
| jt241 | ✅ AUDITED CLEAN 2026-07-13 — case 1 (occupancy compare) init matches | [x] CODE 11+0x5704 |
| jt242 | ✅ AUDITED CLEAN 2026-07-13 — both jt242 case-0 arms (oldval compose / l5ee2 nibbles) match; 0x5df6 is the l5dc8 leaf (preview-paint), also clean | [x] CODE 11+0x58de, [x] CODE 11+0x5996, [x] CODE 11+0x5df6 |
| jt243 | ✅ AUDITED 2026-07-13 — 1 MIS-LIFT FIXED in the l429c helper (0x42b2: case 1, the area map, was missing its opening jt77() frame-chrome call — LIVE, l28d4/l2f1c call l429c(rec[4],1)). jt243 proper clean (0x0b7a was already caught+fixed in a prior session — the picker-return arm; 0x0e26/0x0f46/0x0f58 match); l2836/l28d4/l3380/l5dc8 clean (l28d4 0x2c46 nit: the C masks (cmd>>8)&0xff where the asm asrw does not — harmless for real menu ids, outside the straddle scope) | [x] CODE 11+0x0b7a, [x] CODE 11+0x0e26, [x] CODE 11+0x0f46, [x] CODE 11+0x0f58, [x] CODE 11+0x286e, [x] CODE 11+0x2a08, [x] CODE 11+0x2c46, [x] CODE 11+0x33ee, [x] CODE 11+0x42b2 |
| jt247 | ✅ CLEAN 2026-07-13 | [x] CODE 2+0x23f0 |
| jt249 | ✅ CLEAN 2026-07-13 | [x] CODE 2+0x387c |
| jt250 | 🔴 MIS-LIFT FIXED ×2 — case 2/3/4/7 lost `flag = 1` (wrongly ran the jt325 commit block); and the j325r==3 pack was <<16 where the asm zero-extends into the LOW word 2026-07-13 | [x] CODE 2+0x4098 |
| jt253 | ✅ CLEAN 2026-07-13 | [x] CODE 2+0x4558 |
| jt254 | LIFTED — AUDIT the arm | [x] CODE 2+0x4e1e, [x] CODE 2+0x4e92 |
| jt258 | ✅ CLEAN 2026-07-13 | [x] CODE 2+0x0234, [x] CODE 2+0x0f48, [x] CODE 2+0x1d92 |
| jt259 | ✅ CLEAN 2026-07-13 | [x] CODE 10+0x4580 |
| jt264 | ✅ CLEAN 2026-07-13 | [x] CODE 10+0x6364 |
| jt266 | ✅ CLEAN 2026-07-13 | [x] CODE 10+0x1d54, [x] CODE 10+0x1ea6, [x] CODE 10+0x2a5c, [x] CODE 10+0x2e80 |
| jt269 | 🔴 MIS-LIFT FIXED ×3 — all three *outp packs used <<16 where the asm swap/clrw/swap ZERO-EXTENDS into the LOW word (which is where the next call parses the command back out) 2026-07-13 | [x] CODE 10+0x005a, [x] CODE 10+0x00f6, [x] CODE 10+0x0348, [x] CODE 10+0x07ec, [x] CODE 10+0x0ea6 |
| jt292 | ✅ CLEAN 2026-07-13 | [x] CODE 22+0x1700, [x] CODE 22+0x175a |
| jt311 | ✅ CLEAN 2026-07-13 | [x] CODE 22+0x1b22 |
| jt323 | LIFTED — AUDIT the arm | [x] CODE 9+0x1102 |
| jt326 | ✅ CLEAN 2026-07-13 | [x] CODE 9+0x04a6, [x] CODE 9+0x072a, [x] CODE 9+0x0980 |
| jt340 | ✅ CLEAN 2026-07-13 | [x] CODE 8+0x5ae2 |
| jt346 | ✅ CLEAN 2026-07-13 | [x] CODE 8+0x7082 |
| jt370 | ✅ CLEAN 2026-07-13 | [x] CODE 8+0x6f24 |
| jt377 | 🔴 MIS-LIFT FIXED — the ENTIRE case-1 paint arm was absent (the C delegated every cmd to l1676); shape-6 label never drew. jt1089 arm now lifted 2026-07-13 | [x] CODE 3+0x27dc |
| jt378 | ✅ CLEAN 2026-07-13 | [x] CODE 3+0x25ba |
| jt382 | ✅ CLEAN 2026-07-13 | [x] CODE 3+0x1a5e |
| jt412 | ✅ CLEAN 2026-07-13 | [x] CODE 3+0x38a0 |
| jt433 | ✅ FIXED pre-audit — THE canonical straddle victim: the form-feed arm lost `jsr L4854` (PrClosePage) and was recovered from raw bytes (docs/gdos-printing-wall.md) | [x] CODE 3+0x49c4 |
| jt443 | ✅ CLEAN 2026-07-13 | [x] CODE 3+0x16e6 |
| jt491 | ✅ CLEAN 2026-07-13 | [x] CODE 13+0x3176, [x] CODE 13+0x31ec, [x] CODE 13+0x5584 |
| jt505 | ✅ CLEAN 2026-07-13 | [x] CODE 13+0x73be |
| jt511 | ✅ CLEAN 2026-07-13 | [x] CODE 13+0x096c |
| jt538 | ✅ CLEAN 2026-07-13 | [x] CODE 14+0x21da |
| jt539 | 🔴 MIS-LIFT FIXED — the NEXT-target arm lost `f6 = 1` (its PREV sibling kept it); l315e can clear f6, so the commit gate could be stale 2026-07-13 | [x] CODE 14+0x3d0a |
| jt552 | ✅ CLEAN 2026-07-13 | [x] CODE 14+0x53b8 |
| jt557 | ✅ CLEAN 2026-07-13 | [x] CODE 17+0x6dcc |
| jt560 | ✅ CLEAN 2026-07-13 | [x] CODE 17+0x6374, [x] CODE 17+0x6450 |
| jt563 | ✅ CLEAN 2026-07-13 | [x] CODE 17+0x0ffc |
| jt568 | ✅ CLEAN 2026-07-13 | [x] CODE 17+0x33c2 |
| jt571 | ⏭ UNLIFTED (l24d2 deferred tail) 2026-07-13 | [x] CODE 17+0x3206 |
| jt573 | LIFTED — AUDIT the arm | [x] CODE 17+0x22a6, [x] CODE 17+0x270a |
| jt582 | ✅ CLEAN 2026-07-13 | [x] CODE 15+0x17ce |
| jt604 | ✅ CLEAN 2026-07-13 | [x] CODE 16+0x39ae |
| jt610 | 🔴 MIS-LIFT FIXED — l4faa case 1 lost its leading jt179(1) 2026-07-13 | [x] CODE 16+0x4ff2 |
| jt614 | ✅ CLEAN 2026-07-13 | [x] CODE 16+0x0352 |
| jt623 | ✅ CLEAN 2026-07-13 | [x] CODE 16+0x0772 |
| jt705 | ✅ CLEAN 2026-07-13 | [x] CODE 16+0x3350 |
| jt799 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x5278 |
| jt801 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x533e |
| jt860 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x003a |
| jt867 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x20d8 |
| jt868 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x0466 |
| jt875 | 🔴 MIS-LIFT FIXED — kind-3 worn item: `ev = 3` should be `ev = 18` (0x1ba0 moveq #18). The girdle STR 18/00 grant never applied 2026-07-13 | [x] CODE 18+0x1ba0 |
| jt879 | ✅ CLEAN 2026-07-13 | [x] CODE 18+0x11a6 |
| jt881 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x5940 |
| jt882 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x3152 |
| jt886 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x189e |
| jt899 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x5522 |
| jt902 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x2ff0 |
| jt906 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x69f8 |
| jt910 | ✅ CLEAN 2026-07-13 | [x] CODE 19+0x63e0 |
| jt913 | LIFTED — AUDIT the arm | [x] CODE 19+0x0706, [x] CODE 19+0x0740 |
| jt922 | ✅ CLEAN 2026-07-13 | [x] CODE 12+0x2716, [x] CODE 12+0x2814, [x] CODE 12+0x2ac8 |
| jt928 | ✅ CLEAN 2026-07-13 | [x] CODE 12+0x3258, [x] CODE 12+0x35f4 |
| jt929 | ✅ CLEAN 2026-07-13 | [x] CODE 12+0x3c50, [x] CODE 12+0x3c9c |
| jt932 | ✅ CLEAN 2026-07-13 | [x] CODE 12+0x53e8 |
| jt934 | ✅ CLEAN 2026-07-13 | [x] CODE 12+0x08a4 |
| jt945 | ✅ CLEAN 2026-07-13 | [x] CODE 20+0x6e36 |
| jt948 | ✅ AUDITED CLEAN 2026-07-13 — all 7 arms match; the sites actually live in the l4eea / l4f9a / l6020 / l6436 / l673e helpers (attribution = nearest export) | [x] CODE 20+0x4f6e, [x] CODE 20+0x5036, [x] CODE 20+0x5406, [x] CODE 20+0x6042, [x] CODE 20+0x6302, [x] CODE 20+0x64b0, [x] CODE 20+0x675c |
| jt952 | ✅ CLEAN 2026-07-13 | [x] CODE 20+0x14f0, [x] CODE 20+0x1ed8, [x] CODE 20+0x244c, [x] CODE 20+0x3036, [x] CODE 20+0x3744, [x] CODE 20+0x3e5e |
| jt953 | 🔴 MIS-LIFT FIXED ×2 — case 0 (Move) lost its leading jt176(); and facing case 136 is the WRAP arm (clrb → 0), which the parametrized `cmd-128` lift turned into an out-of-range 8 2026-07-13 | [x] CODE 21+0x40ea, [x] CODE 21+0x44ac |
| jt955 | ⏭ UNLIFTED (PROBE stub) 2026-07-13 | [x] CODE 21+0x456c, [x] CODE 21+0x46aa, [x] CODE 21+0x478c |
| jt956 | ⏭ UNLIFTED (l326e unlifted) 2026-07-13 | [x] CODE 21+0x32f6, [x] CODE 21+0x331e |
| jt957 | ✅ CLEAN 2026-07-13 | [x] CODE 21+0x3156 |
| jt959 | ✅ CLEAN 2026-07-13 | [x] CODE 21+0x1934 |
| jt960 | ✅ CLEAN 2026-07-13 | [x] CODE 21+0x1b82 |
| jt970 | ⏭ UNLIFTED (l3e50 PROBE stub) 2026-07-13 | [x] CODE 5+0x3f3c |
| jt974 | ✅ CLEAN (site is in l157c, the cold-disk dialog, not the sequencer) 2026-07-13 | [x] CODE 5+0x1596 |
| jt992 | ✅ CLEAN 2026-07-13 | [x] CODE 5+0x1e28, [x] CODE 5+0x1ea0 |
| jt1112 | 🔴 MIS-LIFT FIXED — l690e inMenuBar arm lost its leading l012e() (menu enable/disable refresh); L012e + its l6850/l6898 leaves now LIFTED 2026-07-13 | [x] CODE 4+0x6948 |
| jt1121 | 🔴 MIS-LIFT FIXED — l731e case 3 was a bare `return`; the asm exits only when g_a5_byte(-820) is set, else falls to the mask-refresh path 2026-07-13 | [x] CODE 4+0x7388 |
| jt1129 | ✅ CLEAN 2026-07-13 | [x] CODE 4+0x476a |

## SWEEP COMPLETE — 2026-07-13

All 145 sites audited. **~120 clean, 15 mis-lifts fixed, ~10 unlifted** (nothing
to damage: PROBE stubs and deferred tails). Hit rate ~1 in 10.

The 15 repairs, by damage class:

**Eaten first instruction — the classic.** The lift simply lost the arm's
opening statement: jt233 (`ret = 11`), l429c (`jt77()` frame chrome), jt953
(`jt176()`), jt250 (`flag = 1`), jt539 (`f6 = 1`), l4faa (`jt179(1)`), l642c
(the `hl` argument — became `jt895(index, index)`), l690e (`l012e()`).

**Eaten arm reconstructed WRONG.** Worse than a missing statement, because the
lift invented plausible-looking code: jt875 guessed `ev = 3` from the case
number where the asm says `moveq #18` (the girdle's STR 18/00 never applied);
jt953's facing-136 WRAP arm (`clrb` → 0) got folded into a parametrized
`cmd - 128`, yielding an out-of-range facing 8.

**Whole arm invisible.** jt377's entire case-1 paint arm was gone — the C
delegated every cmd to l1676, so the shape-6 label never drew.

**Arm/default swapped.** l5484 (JT[207], the area-map edge classifier): case 0
returns 1 and the out-of-range default returns 0; the C had them backwards.

**Straddle hid a Mac idiom.** jt269 (×3) and jt250 packed their reply with
`<< 16`, but the asm's `swap/clrw/swap` is THINK C's **zero-extend**, so the
value belongs in the LOW word — which is exactly where the next call parses it
back out. l100c's `rec[13] >= 0` store gate was inverted (`bge` SKIPS the store).

**One gap revealed, not a straddle bug:** jt183 case 0 — the shop's BUY action —
dispatches to **L3c7c, which was never lifted at all**. The eaten instruction
was a call to a missing function, so this is a net-new lift (~148 insns +
l3bcc + l38fe), not a restore. **✅ CLOSED 2026-07-13** — L3c7c + l38fe + l3bcc
lifted and live-verified in Hatari (buy an item, pay from personal coin, then
from the party pool). It also disproved the wall doc's "FRUA has no buy verb"
claim and exposed a formatter bug: THINK C's `%l` is a CONVERSION, not a length
modifier, so the vsprintf shims were emitting nothing for it. See
docs/shop-merchant-wall.md.

Verified: 149 tests pass, clean smoke boot (menu up, zero bus errors).
