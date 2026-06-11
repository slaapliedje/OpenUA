# Stub inventory — genuine lift targets vs. intentional no-ops

A triage of every one-line `jtN` stub left in `src/engine/boot.c`, classified so a
future session knows what is real work versus what is faithful-as-a-stub. Built
2026-06-10 by cross-referencing the trivial-body stubs against the jumptable
(CODE address + disasm size) and the call-frequency audit.

Regenerate: the classifier in the chat that produced this (trivial-body detection
+ `data/work/disasm/jumptable.txt` address lookup + `instr`/`bytes` span to the
function's `rts`). 86 distinct `jtN` stubs total; ~half are genuine work.

`instr` = approximate instruction count in the Mac body (size to the first
`rts`). `calls` = call sites across the decompilation.

---

## 1. Faithful AS stubs — DO NOT lift

These are correct as one-liners; lifting them is wrong or pointless.

| JT | why |
|----|-----|
| jt3 (33i) / jt1 (49i) / jt2 (41i) | THINK C inline-`switch` dispatchers — each *call site* reads its own table (`tools/jt3_extract.py` / `jt1_extract.py`). Not functions to lift. |
| jt1163 (2i) | returns 0 (display-mode predicate, always 0 here) |
| jt1198 (2i) | returns 1 (glyph row-step constant) |
| jt1170 (3i) | empty Mac body (`linkw/unlk/rts`) |
| jt1061 (145i) | 24/32-bit addressing-mode juggling — a genuine no-op on the 68030 despite the size. See its comment. |
| jt1177 (48i) | HAL row-blit; bus-errors on uninit NewGWorld pages. HAL-deferred, leave. |
| jt1130 / jt920 / jt956 / jt949 / jt445 (0–1i) | empty (`rts`-only) or alias shells. |
| jt1137 / jt1152 (2i) | constants — verify, but almost certainly leave. |

## 2. Aliases — DELEGATE ✓ DONE

These shared a CODE address with an already-lifted `lXXXX` (like `jt1085`→`l0088`,
`jt1012`→`l37aa`). Both delegated — and not just cleanup: the targets draw the
bigpic + commit the palette, and were already live at other sites, so the
delegation activates the faithful path at the `jt`-named call sites too.

| stub | = | done |
|------|---|------|
| jt124 (6+0x3eea) | `l3eea` (palette commit) | `jt124(long h)` → `l3eea((void*)h)`; arg already deref'd at the call site |
| jt46 (6+0x534a) | `l534a` (area-overview composer) | `jt46(a,b,c,d)` → `l534a(a,b,c,d)`; l534a NULL-guards internally |

## 3. Genuine lift targets — by subsystem (CODE segment)

Grouped by CODE segment (the segment hints at the subsystem). Sorted big→small
within each. The big ones (≥150i) are real subsystems; the small ones (≤25i)
are quick wins likely over already-lifted leaves.

### CODE 4 — Toolbox shim / Sound Manager
A `jt1181`–`jt1191` family (~136–176i each, freq 1) looks like one subsystem
(Sound Manager / sequenced audio). Worth lifting together, not piecemeal.
- jt1123 (90i), jt1109 (29i), jt1142 (17i), jt1121 (6i), jt1148/jt1130 (4+0x61f8/6),
  jt1199 (22i), jt1183/jt1184/jt1188/jt1189/jt1191/jt1181 (~50i each, the family)

### CODE 5 — THINK C runtime / GLIB / sound
- **jt1044 (798i!)** + **jt1050 (545i)** — the two largest stubs in the build;
  a major CODE 5 subsystem (likely GLIB picture decode or the sound engine).
- jt988 (181i), jt1007 (179i), jt1064 (141i), jt1023 (123i), jt1021 (93i),
  jt1022 (82i), jt965 (47i), jt1024 (25i), jt985 (24i), jt1087/jt1088 (13/16i)
- jt965 / jt985 are the audio leaves left stubbed by the jt52 sound lift
  (see [[band1-tail-triage]]).

### CODE 6 — engine core / play frame
- jt931 (191i), jt19 (65i), jt81 (59i), jt97 (51i), jt919 (44i), jt82 (20i),
  jt29 (13i), jt69 (10i), jt89 (5i), jt90 (4i)

### CODE 7 — view / render / HUD
- jt156 (73i), jt140 (68i), jt138 (38i), jt139 (34i), jt165 (20i), jt217 (7i),
  jt238 (7i), jt161 (4i), jt146 (2i)

### CODE 8 — (unmapped)
- jt365 (130i), jt370 (42i), jt367 (26i)

### CODE 13–14 — area map (next to lifted jt501/jt521, tasks #111/#112)
- jt548 (182i), jt503 (169i), jt527 (4i)

### CODE 15 — play loop
- jt584 (111i), jt590 (81i), jt587 (21i), jt581 (9i), jt593 (14i)

### CODE 17 — character generation (task #101)
- **jt556 (383i)**, jt560 (202i), jt559 (4i)

### CODE 19 — record sheet / combat
- jt907 (186i), jt906 (181i)

### CODE 21 — string/credits
- jt955 (290i)

### CODE 22 — menu / design picker
- jt313 (23i), jt294 (7i)

## 4. `port_*` stand-ins — the parallel gameplay layer

Synthesized port-side mechanics (NOT faithful lifts) flagged by
[[feedback-lift-real-code]] for replacement. These are the largest "stand-in"
debt; each should give way to its faithful CODE 13–19 path.

`port_draw_play_frame` (the dungeon HUD over-blit; → jt304/L3fd8, task #114),
`port_run_encounter`, `port_rest`, `port_play_message`, `port_play_demo`-family
(`port_always_load` / `port_frame_load` / `port_menu_load` / `port_menu_bar` /
`port_ui_group_base` / `port_hud_text_clut`), `port_load_savgame`,
`port_show_intro`.

## Suggested order

1. **Aliases** (§2) — two trivial delegates, removes false "stub" noise.
2. **Small genuine wins** (≤25i) — PARTLY DONE (862989a): jt161/jt217/jt559
   (A5 setters) + jt238 (->jt304) + jt581 (->jt147) lifted. STILL BLOCKED /
   gotchas: jt29 (its L2f74 ≠ the port's `l2f74(void)` — different function,
   signature mismatch), jt294 (uses a 4th arg fp@14 the 3-arg stub lacks),
   jt527 (needs jt120=L3918, ~60i), jt593 (needs jt56), jt82 (needs L3918),
   jt89/jt90 (need jt1182), jt69 (needs l5ac0/l4d7a/jt1081/jt415). Unblock by
   lifting jt120 (frees jt527+jt82) and jt1182 (frees jt89+jt90) first.
3. **A subsystem**: pick one coherent block — the CODE 4 sound family
   (jt1181–1191), the CODE 17 char-gen pair (jt556/jt560, task #101), or the
   CODE 5 giants (jt1044/jt1050) — rather than scattering.
4. **`port_*` replacement** (§4) — the faithfulness endgame; each needs its
   CODE 13–19 path lifted first, so it follows the subsystem work.
