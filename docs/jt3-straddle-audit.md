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
| (pre-export) | CODE 17 pre-export helpers (char-gen tables region) | [ ] CODE 17+0x01a8, [ ] CODE 17+0x02fc, [ ] CODE 17+0x035c, [ ] CODE 17+0x03bc, [ ] CODE 17+0x042e, [ ] CODE 17+0x06c2, [ ] CODE 17+0x0722, [ ] CODE 22+0x00da |
| jt17 | LIFTED — AUDIT the arm | [ ] CODE 6+0x27aa |
| jt21 | LIFTED — AUDIT the arm | [ ] CODE 6+0x1790 |
| jt131 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 6+0x038a |
| jt183 | LIFTED — AUDIT the arm | [ ] CODE 7+0x4024 |
| jt207 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 7+0x54e0 |
| jt224 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 7+0x0898 |
| jt225 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 7+0x0bbe |
| jt233 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 11+0x0320, [ ] CODE 11+0x040a, [ ] CODE 11+0x045a |
| jt241 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 11+0x5704 |
| jt242 | LIFTED — AUDIT the arm | [ ] CODE 11+0x58de, [ ] CODE 11+0x5996, [ ] CODE 11+0x5df6 |
| jt243 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 11+0x0b7a, [ ] CODE 11+0x0e26, [ ] CODE 11+0x0f46, [ ] CODE 11+0x0f58, [ ] CODE 11+0x286e, [ ] CODE 11+0x2a08, [ ] CODE 11+0x2c46, [ ] CODE 11+0x33ee, [ ] CODE 11+0x42b2 |
| jt247 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 2+0x23f0 |
| jt249 | LIFTED — AUDIT the arm | [ ] CODE 2+0x387c |
| jt250 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 2+0x4098 |
| jt253 | LIFTED — AUDIT the arm | [ ] CODE 2+0x4558 |
| jt254 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 2+0x4e1e, [ ] CODE 2+0x4e92 |
| jt258 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 2+0x0234, [ ] CODE 2+0x0f48, [ ] CODE 2+0x1d92 |
| jt259 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 10+0x4580 |
| jt264 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 10+0x6364 |
| jt266 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 10+0x1d54, [ ] CODE 10+0x1ea6, [ ] CODE 10+0x2a5c, [ ] CODE 10+0x2e80 |
| jt269 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 10+0x005a, [ ] CODE 10+0x00f6, [ ] CODE 10+0x0348, [ ] CODE 10+0x07ec, [ ] CODE 10+0x0ea6 |
| jt292 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 22+0x1700, [ ] CODE 22+0x175a |
| jt311 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 22+0x1b22 |
| jt323 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 9+0x1102 |
| jt326 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 9+0x04a6, [ ] CODE 9+0x072a, [ ] CODE 9+0x0980 |
| jt340 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 8+0x5ae2 |
| jt346 | LIFTED — AUDIT the arm | [ ] CODE 8+0x7082 |
| jt370 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 8+0x6f24 |
| jt377 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 3+0x27dc |
| jt378 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 3+0x25ba |
| jt382 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 3+0x1a5e |
| jt412 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 3+0x38a0 |
| jt433 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 3+0x49c4 |
| jt443 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 3+0x16e6 |
| jt491 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 13+0x3176, [ ] CODE 13+0x31ec, [ ] CODE 13+0x5584 |
| jt505 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 13+0x73be |
| jt511 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 13+0x096c |
| jt538 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 14+0x21da |
| jt539 | LIFTED — AUDIT the arm | [ ] CODE 14+0x3d0a |
| jt552 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 14+0x53b8 |
| jt557 | LIFTED — AUDIT the arm | [ ] CODE 17+0x6dcc |
| jt560 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 17+0x6374, [ ] CODE 17+0x6450 |
| jt563 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 17+0x0ffc |
| jt568 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 17+0x33c2 |
| jt571 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 17+0x3206 |
| jt573 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 17+0x22a6, [ ] CODE 17+0x270a |
| jt582 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 15+0x17ce |
| jt604 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 16+0x39ae |
| jt610 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 16+0x4ff2 |
| jt614 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 16+0x0352 |
| jt623 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 16+0x0772 |
| jt705 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 16+0x3350 |
| jt799 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x5278 |
| jt801 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x533e |
| jt860 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x003a |
| jt867 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x20d8 |
| jt868 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x0466 |
| jt875 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x1ba0 |
| jt879 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 18+0x11a6 |
| jt881 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x5940 |
| jt882 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x3152 |
| jt886 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 19+0x189e |
| jt899 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x5522 |
| jt902 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 19+0x2ff0 |
| jt906 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x69f8 |
| jt910 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x63e0 |
| jt913 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 19+0x0706, [ ] CODE 19+0x0740 |
| jt922 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 12+0x2716, [ ] CODE 12+0x2814, [ ] CODE 12+0x2ac8 |
| jt928 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 12+0x3258, [ ] CODE 12+0x35f4 |
| jt929 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 12+0x3c50, [ ] CODE 12+0x3c9c |
| jt932 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 12+0x53e8 |
| jt934 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 12+0x08a4 |
| jt945 | not-in-boot.c (alias? check lxxxx-jt-aliases.md) | [ ] CODE 20+0x6e36 |
| jt948 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 20+0x4f6e, [ ] CODE 20+0x5036, [ ] CODE 20+0x5406, [ ] CODE 20+0x6042, [ ] CODE 20+0x6302, [ ] CODE 20+0x64b0, [ ] CODE 20+0x675c |
| jt952 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 20+0x14f0, [ ] CODE 20+0x1ed8, [ ] CODE 20+0x244c, [ ] CODE 20+0x3036, [ ] CODE 20+0x3744, [ ] CODE 20+0x3e5e |
| jt953 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x40ea, [ ] CODE 21+0x44ac |
| jt955 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x456c, [ ] CODE 21+0x46aa, [ ] CODE 21+0x478c |
| jt956 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x32f6, [ ] CODE 21+0x331e |
| jt957 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x3156 |
| jt959 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x1934 |
| jt960 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 21+0x1b82 |
| jt970 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 5+0x3f3c |
| jt974 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 5+0x1596 |
| jt992 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 5+0x1e28, [ ] CODE 5+0x1ea0 |
| jt1112 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 4+0x6948 |
| jt1121 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 4+0x7388 |
| jt1129 | PROBE stub (nothing lifted — SAFE) | [ ] CODE 4+0x476a |

High-traffic owners to sweep first: **jt948** (the dungeon loop, 7 sites),
**jt243 / jt266 / jt269** (design-editor painters), **jt491**,
**jt922/jt928/jt929** (combat-adjacent CODE 12), **jt974** (the music
sequencer), **jt953-jt960** (CODE 21 band).
