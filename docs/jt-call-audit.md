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

### l036a error dialog — actual breakdown (mapped + partly lifted 2026-06-08)

l036a(fmt, ...): jt1116 save-state; jt1205; jt1193; jt1153(1); jt1161 box;
L024c(240)+L0264+L0306("Error: %r",va) text; jt1147; L00a8 drain; wait
(L0088); on key 'q' (113) -> L0062 quit + jt415(1); L00a8; L024c(15)+jt1161
erase; jt1167; jt1153(restore).

- DONE (ac899c1): the event + pen primitives — jt437-441, L0088, L00a8,
  L024c, L0264 (+ jt1108/jt1137). All faithful over the lifted event buffer.
- jt1161/jt1118/jt1133/jt1125/jt1135/jt1153 already lifted.
- **BLOCKER — L0306 text draw needs jt400 (CODE 3+0x3fb8, ~489 instr): a full
  printf-with-output-callbacks engine** (%r/%s/%d/%lx/%03d…, sinks jt966-969,
  ~33 instr each). DECISION NEEDED: transcribe verbatim, or map L0306 onto the
  port's existing format path (jt394/jt488 sprintf) + the GLIB char draw —
  per the Mac-Toolbox-shim architecture (ADR-0003) string formatting is a shim
  concern, arguing for reuse not re-lift.
- still missing (CODE 4 display-state, small — TBR): jt1116, jt1205, jt1147,
  jt1167; jt1193 (stub), jt415 (stub, quit/abort).
- L0062 quit path (only on 'q'): jt466/1156/1119/1114/1158 + L27bc/L35f8(done)/
  L01ac/L0f14 — defer (rare abort branch).

### l036a DONE (7d0088c)
Lifted faithful-on-shim: jt1116/jt1205/jt1167/jt1147 (+ SysBeep shim), L0062
skeleton, l036a itself. The jt400/%r text path mapped onto jt1089 (the
established vsnprintf+DrawString equivalent). Event/pen prims were ac899c1.

### jt987 (gatekeeper #2) — anatomy + next target
jt987 (CODE 5+0x1a0c) is NOT the loader — it's a **load-with-retry-dialog
wrapper**: it calls the real loader L17e2 and, on failure, shows the
"please insert disk / Cancel / Quit" dialog (L157c) and retries. For the
port (files on GEMDOS disk) L17e2 should just succeed, so the retry UI is
cold. jt987 deps mostly lifted/skeletoned: L036a(done), L0062(skeleton),
jt415(stub), event prims(done); still need jt1152/jt1142/jt1121 (cursor +
event), jt408, L157c (the disk dialog), L0156.

### L17e2 + jt987 — ASSEMBLED + HATARI-VERIFIED (0d9132a)

Both lifted (faithful CFG; cold save/dialog arms skeletoned). Verified in
Hatari against real game data (data/work/gamedata, --conout 2 trace) with a
throwaway probe in ua_main Phase 3:
  CHECKPOINT l17e2 ALWAYS.CTL  = 1   (open refnum 64 -> jt411 close -> 1)
  CHECKPOINT jt987  ALWAYS.CTL = 1   (wrapper, first try, no retry dialog)
  CHECKPOINT l17e2  MISSING.CTL = 0  (3 retries, refnum -1 -> 0)
Trace confirmed the path: L17e2 -> jt420/jt408/jt389 classify -> L16c6 path
-> jt398 (l322c/l45d6/l328e) -> refnum -> jt411. The opener works.

### jt104 read callback — decoded; reader FOUNDATION lifted

jt104 (CODE 6+0x3214) finds the item by id (jt1013), loads it (jt1011), then
stores it into the GLIB group slot. KEY SPLIT by mode (g_a5_-18398, set by
L33ac):
- **mode 0 (the picture .ctl case): JT[1016]** — read into the group slot.
- mode 1/2 (TLB/title): jt462 release + jt1024 dir-create + jt1021 + jt1023.
  COLD for pictures.

The group table jt462/jt468 use is g_a5_-10074 (jt468 already resolves it —
NO rewiring needed). The item handle table is g_a5_-10270.

DONE (this session): the GLIB library readers jt412 (seek -> SetFPos/GetFPos),
jt1011 (load item), jt1013 (find by id). The 'GLIB' header + index format.

DONE (read-into-pool layer, this step): the FAR-pool model is one
contiguous buffer; g_a5_10270[i] (longs) = START *pointer* of the i-th
group, g_a5_10270[count] = used end, g_a5_9304 = capacity end, g_a5_9306
= group count, g_a5_10074 = 48-byte freemap (id->seq, 0xFF free). NO
decompression at load — raw bytes only; codecs run at blit time
([[glib-art-codecs]]). The file I/O collapsed onto already-lifted shims:
L3888 (seek) = jt412, L3d98 (read) = jt401/FSRead. Lifted:
- jt459 (CODE 3+0xd44): size query — id>=0 group size, -2 capacity,
  -1 free. FULL.
- l3e0c (=JT[409]): find-byte helper. FULL.
- L11ca (CODE 3+0x11ca): one compaction pass; jt1083 RNG picks orphan
  scan order; releases via L103c. FULL.
- L0a6e (CODE 3+0xa6e): ensure free tail, compacting until enough. FULL.
- L0ab8 (CODE 3+0xab8): extend the in-progress group (slot[count]+=size),
  track g_a5_9300 low-water. FULL.
- jt460 (CODE 3+0xc0a): append `length` raw bytes (neg = read-to-EOF);
  on jt412(seek)+jt401(read)+l0ab8. FULL.
- jt462 (CODE 3+0xb16): unwind the in-progress group on failure. FULL.
- jt1016 (CODE 5+0x3640): driver — jt460 read + jt459 size + L4010
  commit; jt462 on fail. FULL CFG.

DONE (commit/relocate step): L4010 (_LBConvert) FULL. Key finding: the
converter registered for 'GLIB' is JT[973], and JT[973] == L4010 itself
(CODE5+0x4010) — so the index relocation is RECURSIVE descent over
GLIB-of-GLIBs, bottoming out at leaf art whose signature isn't in the
registry. Lifted alongside:
- l4010: validate magic, odd-pad, then walk the 16-byte-header index;
  for each entry call the signature's converter (recurses), accumulate
  the size delta, rewrite each index entry + the header in place.
- glib_lb_register (L35fa) / glib_lb_init (L35e2): the converter
  registry (-3654 sig / -3638 fn / -3656 count); init registers exactly
  'GLIB' -> l4010. MUST be called once before the pool loads a library.
- l3e50 (the hdr[10]!=0 typed-.tlb sub-convert arm): PROBE stub
  (identity); plain UI .ctl GLIBs leave hdr[10]==0, so untaken.

DONE (pool stand-up + extractor + binder + Hatari checkpoint):
- jt463 (_LBOpen): stands up the FAR pool — group count 0, freemap 0xFF,
  alloc the master buffer (fixed 768K), seed slot[0]/-9304/-9300, register
  'GLIB'->l4010 via glib_lb_init.
- jt104 (CODE6+0x3214): the per-file callback jt987 invokes after open;
  finds the binder-context item (-18408 base id, +100*-18404 fallback),
  sizes it, and for mode 0 (-18398==0, hot) commits via jt1016. Modes 1/2
  (TLB cache) are a faithful skeleton over jt1021/1023/1024 stubs.
- L33ac (JT[110], the binder): level-2 skeleton — claims a -18468 slot,
  builds the filename, stamps the context (-18402/-18406/-18408/-18404/
  -18398), and dispatches: plain names -> jt997, numbered -> jt987+jt104.
  jt464/jt997/jt1014 cache leaves remain PROBE stubs.
- VERIFIED under Hatari (glib_pool_selftest, GEMDOS_DIR=gamedata): jt463
  pool capacity=786432; ALWAYS.CTL (5368B) read via jt1016 -> group0
  size=5368, group0 magic=0x474C4942 ('GLIB'). The read+commit layer
  parses real FRUA data end-to-end. All pool code is probe-only/unused,
  so the production build + live buffered UI loader are untouched.

REMAINING:
- jt464 (cache-index existence) + jt997/jt1014 plain-name loader tower —
  needed to load the no-suffix UI .ctls (ALWAYS/FRAME) through the
  faithful path rather than the manual self-test bind.
- Wire jt463/glib_lb_init into the real init path + replace the live
  l37aa/l2856 buffered loader with the faithful pool (the flip).
- palette (jt1069/1066/993/1017), de-skeleton L541a/L579e/L3eea.

### L17e2 (CODE 5+0x17e2) — decoded (historical notes)

L17e2(kind, name, arg14_mode, callback) is a 3-attempt resource-file opener:
build the path (L16c6), open by mode, run the caller's `callback(refnum,
filespec)` to read it, close (jt411); on failure show the disk-retry dialog
(L157c) and retry. KEY SPLIT:
- **mode 3 = READ/LOAD (the picture case): uses jt398 (open, DONE).**
- mode 1/4 = WRITE/SAVE: uses jt392 (create) -> L3386 + L341a (save dialog).

Leaves now LIFTED: L16c6 path builder, jt408/jt420 classifiers (jt422
already done), L00da (wait), L0156 (cursor flash), and the open helpers
jt398/jt411/l322c/l31fc/l45d6/l328e were already lifted. jt1122/jt1134 lifted.

REMAINING to close L17e2 + jt987 (next session):
1. jt416 (CODE 3+0x35d6) -> L45d6 + jt1054 (CODE 5+0x5b74): the mode-3 load
   finalize. Lift jt1054 (check size), then jt416.
2. Cold-path skeletons (rare in the port — files are on GEMDOS disk):
   L157c (disk-retry dialog ~111), jt1109 (post-load notify ~108).
3. SAVE path: jt392 + L3386 (create ~53) + L341a (save dialog) — or skeleton
   jt392 (not needed by the picture subsystem).
4. Assemble L17e2's CFG (gate/mode-switch/retry loop), then jt987's thin
   retry-dialog loop over it.
Then: L33ac binder, palette path (jt1069/jt1066/jt993/jt1017), de-skeleton
L541a/L579e/L3eea. The loader WANTS asset-based Hatari verification (does it
open + parse a real .ctl/.tlb?) — verify mode-3 load before building on it.

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
