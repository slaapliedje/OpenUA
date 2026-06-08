# JT call-frequency audit

A porting-priority tool (idea: lift the most-shared functions first, then the
stand-ins fall away and the rest composes). Counts how often each `JT[N]` entry
is *called* across the whole Macintosh decompilation, cross-referenced with the
port's lift status.

## Regenerate

```sh
# call frequency across all CODE segments (from dis68k's (JT[N]) annotations)
grep -rhoE "\(JT\[[0-9]+\]\)" data/work/disasm/CODE_*.s \
  | grep -oE "[0-9]+" | sort -n | uniq -c | sort -rn > /tmp/jt_freq.txt
```

Then cross-reference `/tmp/jt_freq.txt` against the `jtN` definitions in
`src/engine/boot.c` (a one-line body that is only `PROBE(...)` + `(void)` casts
+ `return 0` is a true stub). 1205 distinct JT entries are called; ~63 of the
port's `jtN` are still one-line stubs.

## The shared foundation (top-called — verify these stay FULLY lifted)

These are the load-bearing primitives; most are already lifted, which is why
recent UI/HUD work composed cleanly. If any regresses to a stub, everything
above it breaks.

| JT | calls | what |
|----|-------|------|
| jt3 | 307 | THINK C inline `switch` dispatch |
| jt384 | 287 | string copy |
| jt1200 | 187 | display-mode query (deep gate) |
| jt488 / jt394 | 156 | sprintf-style format |
| jt94 | 155 | text draw |
| jt406 | 153 | BlockMove / memmove |
| jt1161 | 147 | PaintRect (fill) |
| jt1089 | 143 | formatted text draw |
| jt399 | 126 | memset/fill |
| jt1135 | 83 | 8000-space → screen coord scale |
| jt452 | 81 | DLItem stream builder |
| jt468 / jt1001 | 64/69 | GLIB group lookup / glyph blit |
| jt117 / jt112 / jt108 | 56/43/30 | present / paint-mode / commit |

## High-leverage TRUE stubs to lift (most-called, real work pending)

The actionable queue — sorted by call count. Lifting these removes the most
stand-in pressure.

| JT | calls | what (CODE addr) |
|----|-------|------------------|
| ~~jt96~~ | 43 | DONE — word-wrap text-in-box subsystem |
| ~~jt23~~ | 37 | DONE — play-frame redraw dispatcher (603facc) |
| jt1084 | 34 | setter (buf, val) |
| ~~jt938~~ | 27 | DONE — HUD clock (9a1b42b) |
| ~~jt358~~ | 27 | DONE — counter (d69fceb) |
| jt1193 | 24 | (CODE 7) view-prep tail |
| jt876 | 22 | popup action handler (CODE 18+0x1666) |
| jt1177 | 22 | row-blit draw primitive (HAL-deferred) |
| ~~jt273~~ | 22 | DONE — deep-mode flag (d69fceb) |

Remaining high-leverage TRUE stubs: **jt1084, jt1193, jt876, jt1177**.

## Genuine no-ops / constants — faithful AS stubs, do NOT "lift"

Verified against the Mac body; leave them.

| JT | calls | Mac body |
|----|-------|----------|
| jt1170 | 24 | empty (`linkw/unlk/rts`) |
| jt1198 | 30 | returns 1 always (glyph row-step constant) |
| jt1163 | 36 | returns 0 |
| jt949 | 2 | empty (`rts`) |

## Progress

- **jt913 + jt938 lifted** (9a1b42b): the game clock / position panel. jt938
  runs in jt948's faithful arms; making it VISIBLE in the jt240 arrow-walk is
  a follow-up (same HUD integration the command bar got).
- **jt96 subsystem lifted** (9fb7024): jt390 + l433a + l42a0 + jt96 (word-wrap
  text-in-box). Slow-text (l435a) + pagination (l4c46) arms stubbed. Visible
  wiring (jt18/jt20 record sheet, drop cg_view_sheet) is the follow-up.
- **jt96 fully de-stubbed** (9ab51a0): l435a/l4c46 + the 13-fn pause/pacing
  cluster lifted (all bottom out on already-lifted leaves).
- **jt937 / jt32 / jt34 lifted** (6870674): jt937 (=L02dc roster grid) was
  already faithful but called two stub column drawers — jt34 (THAC0/AC,
  p[385]-60 signed) + jt32 (HP cur/max) lifted, plus helpers jt478/jt388/
  l60b4. l02dc's loop restored to the faithful colour-band form (the jt94
  "%d" stand-ins dropped). NOTE: jt937 does NOT call jt96 — the roster uses
  jt94/jt103/jt25/jt32/jt34. jt96's live wiring is jt18/jt20, still pending.
- **jt23 lifted** (603facc): the play-frame redraw dispatcher. Full CFG (gate
  + 11-case mode switch) + the stand-up spine (L670c/L534a/L3804/L3880) full;
  the backdrop-picture helpers (L541a/L5822/L579e/L3eea) are level-2 skeletons
  pending the GLIB picture subsystem.

## jt23 follow-up: the GLIB picture subsystem (== task #105 territory)

jt23's backdrop arms (cases 2/6, the L5822 full-refresh, and the L3eea
sprite/palette commit) call into a coherent unlifted subsystem worth a focused
lift:

- **L33ac** (CODE 6, ~204 instr) — the PIC resource decode + blit core.
- **L541a** (CODE 6, ~235 instr) — PIC name builder (PIC%c1 / %s%s / bigpi%c%d
  variants over the area id) feeding L33ac.
- **L579e** (CODE 6) — bigpic loader (cached on g_a5_-24256/-17446).
- **jt993** (CODE 5+0x20d0, TNPalette) + **jt1017** (CODE 5+0x38be, LBIndxType)
  — the palette commit, pulling L2856 (library lookup) + jt1069 (palette set).
- leaf helpers: L035e (group set, -> jt204/jt209/L5700/L5864), L338c, L31dc,
  L3f3c (-> jt1066/jt1069).

These are the screen-backdrop / palette path; lifting them lights up the
play-screen picture window + the cases-2/6 area backdrops.

### Dependency map (mapped 2026-06-08) + lift sequencing

Already lifted (reuse): jt384, jt394, jt419, jt423, jt431, jt398, jt411,
jt461, jt468, jt406, jt1200, jt1163, jt1134, l2856, jt204, jt209, l5700,
l5864, jt1066? (no — stub).

The load path bottoms out in TWO gatekeepers that are event-loop / dialog
code (NOT verifiable by reading — need the real assets in Hatari):

- **jt987** (CODE 5+0x1a0c, ~120 instr, 16 call targets) — the library-file
  open + read loop with a progress/error dialog (jt1118/jt1133 event poll,
  jt1152/jt1142/jt1121, jt415/jt408, l036a, + l157c/l0156/l00a8/l0f9c/l0088/
  l0062 sub-functions).
- **l036a** (CODE 5+0x36a, modal "Error: %r" dialog) — draws an alert box
  (jt1161) + its own key-wait loop (jt1116/jt1205/jt1193/jt1153/jt1147/l024c/
  l0264/l0306/jt1118/jt1133/l0062). jt1069 + jt1066 also route errors here.

Mid-layer (closes ONLY once jt987/l036a land):
- jt997 (CODE 5+0x27be, ~29) -> jt419 + L36a4.  L36a4 (CODE 5+0x36a4, ~43)
  -> jt464 + jt987 + jt468 + jt406 + l036a (verifies the 'GLIB' magic).
- L33ac (CODE 6+0x33ac, ~204) — the binder: finds a free -18468 group slot,
  builds the .ctl/.tlb filename, opens (jt464/jt398/jt431/jt460/jt411), and
  loads via jt987 (name path) or jt997 (id path); sets the -18402.. blit
  descriptor + calls jt104/JT[987].

Palette path (self-contained-ish, HAL-verifiable but long; routes errors to
l036a):
- jt1069 (CODE 5+0x71b0, ~329) — walk the -3258 palette table, set CLUT.
  Deps: jt406, jt1134, l01ae, l036a + one CODE4 JT.
- jt1066 (CODE 5+0x759a, ~200, currently a STUB) — palette save/restore over
  the -3162/-3258/-3354 tables (jt406).
- jt993 (TNPalette) + jt1017 (LBIndxType) — the L3eea commit, over jt1069.

Still-missing small leaves: jt389 (DONE), l31dc (DONE), jt464, jt460, jt104,
l01ae, l024c, l0264, l0306, l0062, l0088, l00a8, l0f9c, l0156, l157c.

**Recommended sequencing** (each its own verified commit):
1. leaves: jt389 + l31dc (DONE, 5dadbbb+).  Then jt464/jt460 (CODE3 file
   tests over the resource archive — check against compat/resources.c).
2. l036a error dialog (gatekeeper #1) — verify the alert renders in Hatari.
3. jt987 loader loop (gatekeeper #2) — verify a .ctl/.tlb opens + reads.
4. L33ac binder + jt997/L36a4 — de-skeleton L541a/L579e.
5. jt1069 + jt1066 + jt993/jt1017 — de-skeleton L3eea (palette commit).
The codec/loader interiors (2-5) should be lifted one at a time with a
Hatari checkpoint each; blind transcription of all ~800 lines at once is not
verifiable.

## jt96 is a SUBSYSTEM, not a one-shot lift (mapped 2026-06-08)

jt96 (43 sites) is a **word-wrap text-in-box renderer** for record-sheet /
roster cells (driven by jt18/jt20). It is NOT a single function — a faithful
lift pulls in a cluster:

- **jt96** (CODE 6+0x43c4, ~150 instr): bounds-check (page/row/width 0..39),
  cell-cache (g_a5_-27912 page / -27911 row), jt103 box if s7!=0, strlen
  (jt483), then a word-boundary scan that measures words against the cell
  width (arg `width`) and wraps lines.
- **L433a** (tiny): is-this-char-a-delimiter — JT[390] lookup in the set
  `"()[]{}-.,?!\":;"`. Needs **jt390** (char-in-set, CODE 3+0x3e3c).
- **L42a0** (~51 instr): draw one text run (the per-substring blit).
- **L435a** (~28 instr) + **L4c46** (~7 instr): line-advance / cell helpers.
- also JT[476] (CODE 3+0x46a), JT[176] (CODE 7+0x162e).

~250 instr across 5–6 functions, HIGH blast radius (43 callsites span the
record sheet + the play roster). Do it as a focused effort: leaf-first
(jt390 → L433a → L42a0 → L435a/L4c46), then jt96, then drop the port's jt94-
based l02dc roster stand-in for the faithful jt18/jt20 → jt96 path.

## Caveat

`(JT[N])` counts are *static call sites* in the disasm, not runtime hotness —
but for "what's most depended-on across the code" they're the right proxy. The
list above counts single-line stubs only; a few heavily-called multi-line
functions (jt1134, jt878, jt1061, jt936, jt868, jt935…) are partial lifts worth
spot-checking individually.
