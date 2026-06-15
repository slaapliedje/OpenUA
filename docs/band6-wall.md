# Band 6 campaign (ranks 501-600) — worklist

`tools/jt_progress.py --band 6` (2026-06-15): 31/100 done, 22 STUB, 47 MISSING
(69 open). But the band-6 open set is NOT 69 genuine lifts — most of it belongs
to other tasks or is shim-routed. Triaged by CODE segment:

| CODE seg | open | disposition |
|---|---|---|
| 13/14/15/16/18/19/20 | 37 | COMBAT — belongs to #115 (CODE 13-19 combat tier), not this band sweep |
| 17 | 4 | CHAR-GEN — belongs to #101 |
| 5 (traps 4d3a..5dc6) | 8 | Mac MEMORY/FILE trap glue (jt1026=_FreeMem, jt1030/1031=_NewHandle, jt1034=_HUnlock, jt1043/1044/1049/1057=GEMDOS). Port handles these AT THE CALL SITE via the compat/ shim (FreeMem/NewHandle/HUnlock) — "faithful-as-shim-routed", NO jtNNN body needed. Classifier counts them MISSING; they are not real work. |
| 5 (jt1079) | 1 | already lifted as master_init (CODE 5+0x4) — classifier naming miss, NOT missing |
| 3, 5 (real fns), 4, 8, 12, 21 | ~19 | the GENUINE band-6 work |

So the real band-6 surface is ~19 utility/misc lifts, not 69.

## Genuine targets (priority order)

PRIORITY 1 — jt433 (CODE 3+0x49a2) the ONLY live STUB (3 call sites in the
  record-sheet printer). Char-output: JT[3] switch min=10 max=12 — case 10
  (LF) bumps the -9154 line counter, pages at 66 lines (calls jt433(12) =
  L4806 new-page), GetPen + advance; case 12 (FF) -> L4806; default -> L4a06
  (draw the glyph). DEPS NOT YET LIFTED: L4806 (new-page) + L4a06 (draw-char) +
  the pen/line state (-9154/-9146/-9163). A small "printer" subsystem lift
  (~3-4 fns). The record-sheet printer (caller at boot.c ~38395) is the value.

PRIORITY 2 — CODE 3 self-contained string/buffer leaves (MISSING, no live
  callers yet — breadth-first):
  - jt402 (CODE 3+0x45d6) = c2pstr: dst[0]=len, dst[1..]=src, trailing NUL.
  - jt383 (CODE 3+0x4782) = emit one byte to the write-cursor at -9168, bump it.
  - jt487 (CODE 3+0x00a2): list/string processor (deps L3e3c unlifted, L3738=l3736).
  - jt418 (CODE 3+0x32e2): ~154-byte-local formatter — bigger, defer.
  - jt435 (0x52c0), jt427 (0x539a): inspect.

PRIORITY 3 — CODE 5 real functions: jt1051 (0x5a0a, link -122), jt1073 (0x7a0e),
  jt1087/1088 (0x012c/0x00a8), jt994 (0x1f22), jt999 (0x309c, a blitter — h-first
  args per [[mac-blit-ground-truth]]). Inspect each.

PRIORITY 4 — misc singletons: CODE 4 (toolbox/event), CODE 8, CODE 12 (HUD),
  CODE 21 (strings). Inspect.

## Progress
- 2026-06-15: triage + worklist (this file). Lifted jt402 (c2pstr) + jt383
  (emit-byte) — the two clean self-contained CODE 3 leaves. NEXT: jt433 printer
  subsystem (P1) as the first real exercised lift.
