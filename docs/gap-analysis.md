# Functionality gap analysis — path to a finished port (2026-06-20)

A PLAY-FLOW view (what the player experiences) of what's lifted+wired vs
stand-in/stub/missing. Complements `docs/roadmap.md` (the by-CODE-segment view)
and the auto-generated `docs/jt-lift-progress.md` (counts). Synthesised from a
3-agent survey + this session's work.

Legend: ✅ works end-to-end · 🟡 partial / buggy · 🔴 gated/missing · ⏸ deferred

## The play flow

| # | Functional area | Status | What works / what's missing |
|--:|-----------------|:------:|------------------------------|
| A | Boot → title → main menu → **Select a Design** | ✅ | Foundation (CODE 1/3/4/5/6/7/8) + front door done. Picker verified. |
| B | **Char-gen** + **Training Hall** + party mgmt | ✅ | Create/Modify/Train/Add/Remove/Delete + roster; party model is the faithful -27928 list (#141). |
| C | **Save / Load** | 🟡 | Party round-trip done (#141). Pending: ~10KB design-state block, A–J slot pickers, boot auto-load. |
| D | **Dungeon entry + walk** | ✅ | Walks + turns with arrows (HEIRS); per-step `l709e` fires on the new cell (Gap-1 closed). |
| E | **Dungeon 3D render** | 🟡 | Navigable corridor renders, but TWO known bugs: (1) **wall-piece placement clips the left ~2/3 columns** (render-X off-screen — the unfinished mirror of the b945821 right-side fix, `jt199`/`jt200`/`l5b42`→`l309c`); (2) **#129 frame "nuked"** by the event-picture FAR-pool stomp. |
| F | **Dungeon EVENTS** (`l709e`, 39 arms) | 🟡 | **16 handlers lifted** (text `l4d26` 2/14, give-treasure `l28b0` 3/25, stairs `l5676` 5/11/34, tavern `l4f9a` 7, shop `l5586` 8, temple `l216a` 9, conditional/stat-check `l1ad8` 15, vault `l3a32` 24, **pass-time `l5fcc` 27 ✅, set-rumors `l661c` 37 ✅, set-flag `l66cc` 38 ✅ NEW**, combat-prompt `l3b0e`/`l673e` 10/21, Yes/No question `l3118` 36). **~17 still STUB** — and the next cheap ones gate on subsystems (see below). |
| G | **Treasure / shops / vault** | ✅ | **Slice B complete** this session: the picker UI (`jt183`/`jt185`/`jt929`/`jt921`/`jt922`/`jt924`/`l2ebc`) + the 3 triggers (shop/temple/vault) + vault file I/O + the bucket-init fixes that make the caravan reward (100pp + ring) actually land. |
| H | **Combat** | 🔴 | Effects engine (CODE 18) **96% done** — but the SPINE is stubbed (`l076e` per-actor turn + `l4f22`/`l0434`/`l102a`/`l0116`); `jt511` loop is lifted but has **no live caller**; field render (CODE 14) + 81 CODE-16 handlers gated on the spine. Multi-session. |
| I | **Rest / camp** (CODE 21), **spell memorize**, **inventory** (CODE 9) | 🟡 | **Camp-menu scaffold lifted (slice 1, 2026-06-21):** the Encamp command (g_walk_cmd 4) + inn now open the real camp menu (`l473e`→`jt957`); View/Save/Load/Exit work. The 5 deep camp actions (`l1850`/`l09ea`/`l1e44`/`l2d7e` + `l038a`) are PROBE stubs — incl. the actual rest/heal. See `docs/code21-camp-wall.md`. |
| J | **Editor / design tools** (CODE 2/11/22) | ⏸ | Deferred by ADR-0008 (runtime first). |

## The frontier in detail

### F — Event-handler vocabulary (the cheapest high-value work)
Now that the walk fires `l709e` per-cell, each handler is **independently
liftable AND live-testable**. Highest value STUBs (ranked):
- ~~`l159a` (cases 1 & 33)~~ — CORRECTION: NOT plain text. It prints "A battle
  begins..." and calls `jt511`/`jt510`/`jt512` — it's the **Monster/Combat
  event**, gated on the combat spine (§H). The real plain-text event is `l4d26`
  (case 2), already lifted. (The stale doc table mislabelled this.)
- ✅ **`l3118`** (case 36) — Yes/No **QUESTION** event — **DONE 2026-06-20**.
  Paint picture/3D → question text → Yes/No modal → yes/no response text; returns
  the answer (l709e case 36's ev[8]/ev[9] branch + `l3bee` were already wired).
  Gates non-linear design.
**CORRECTED event-type map (2026-06-20, by READING the handlers — the
play-loop-wall.md / earlier-agent labels are STALE/WRONG):**

| case | handler | ACTUAL event type | size | status / note |
|-----:|---------|-------------------|------|----------------|
| 7    | `l4f9a` | **TAVERN** ("The party enters a tavern.") + drink/listen/fight/leave menu | ~543 ln | ✅ DONE 2026-06-21 — `l4f9a` + `l4eea` drink sub-prompt. Rumor list = ev[8..15] + globals -4930..-4920; drink strength 6/10/15/30, ≥60 chains ev[19]; fight chains ev[18]. |
| 15   | `l1ad8` | **CONDITIONAL / stat-check** branch event (up to 5 jt155 menu rows; party-min vs ev[8] threshold; retry counter ev[7]&3+1; timeout ev[14]&15) | ~287 ln | ✅ DONE 2026-06-21 — `l1ad8` + `l1528` party min/max helper. Sets result flag -5238 (1/2/3) + chains ev[17/18/19]. |
| 27   | `l5fcc` | **PASS TIME** — `jt914(ev[8],4)`/`(ev[9],3)`/`(ev[10],1)` advance the calendar | ~27 ln | ✅ DONE 2026-06-21. All deps lifted. |
| 37   | `l661c` | **SET RUMORS** — store ev[8..19] into the 6 standard-rumor globals -4930..-4920 (the tavern/inn read these) | ~57 ln | ✅ DONE 2026-06-21. Pure data; complements the tavern. |
| 38   | `l66cc` | **SET FLAG** — `rec[ev[8]+69] = (ev[7]&4)?0:1` quest-flag gadget | ~42 ln | ✅ DONE 2026-06-21. All deps lifted. |
| 13   | `l380a` | %-based STAT modify; gated on `jt592` = **CODE-15 monster/actor alloc** (`L0d9c`/`L1b74` + bucket -22212) | ~59 ln | STUB — combat-tier dep, NOT a clean leaf. |
| 29   | `l398a` | **INN** ("The party enters a local Inn.") + `L473e` rest trigger | ~57 ln | STUB — gated on `jt957` = **CODE-21 rest** (§I, ~238 ln). Shell is clean; core is the rest subsystem. |
| 22   | `l5bde` | **random-encounter META** — dispatches to tavern/shop/temple/vault/inn (`l4f9a`/`l5586`/`l216a`/`l3a32`/`l398a`) | ~307 ln | STUB — needs the inn (29) + case 6 first. |
| 17   | `l3ac6` | **PLAY SOUNDS** (NOT "secret door"): loop ev[4..13] → `jt52` | ~26 ln | STUB but MUTED — jt52's audio leaves + l40b4 stubbed; defer with the audio subsystem |
| 1,33 | `l159a` | **MONSTER / COMBAT** ("A battle begins…" → `jt511`) | ~1338 ln | gated on the combat spine (§H), NOT plain text |
| 6,12,16,26 | `l2d32`/`l2e42`/`l6020`/`l2b2a` | unclassified (6 = jt918/jt176/jt932 civic-ish; 12 = big, 3D `jt221`+`jt954`; 16 = ~383 ln pure-compute, only JT[3]; 26 = jt52 sound + l427c) | — | READ each before lifting; 16 is self-contained but large. |

TAKEAWAY: after `l3118`, the cheap-clean event leaves are exhausted — the
remaining handlers are each a substantial focused lift (tavern, menu events) or
gated (combat/audio/stub-deps). Treat them like the shop/temple/vault lifts: one
focused session each. The **tavern (`l4f9a`)** is the best next single target
(most player-visible, self-contained civic event). Do NOT trust the old labels;
read the handler first.
DOC CORRECTIONS (carry forward): case 32 `l38bc` = **party-member selection**
(NOT vault); case 9 `l216a` = **temple/take text** (NOT a reward picker).
`docs/play-loop-wall.md` Gap-2 table (lines 77-106) is now stale.

### E — the 3D render placement bug (your "columns not loading")
NOT a load bug — `l0bbc`/`jt198`/`l7226` copy the whole 576-cell `MAP ` chunk
(3456 B) in one BlockMove; `l5e52`'s `cell = col*ds[3] + row` is byte-faithful
col-major. The left columns are **drawn off-screen and clip**. Look at `jt199`
LEFT sub-loops (`boot.c:~10832-10897`, the `soff -= 2` family) vs RIGHT, and the
screen-X after the b945821 swap (`l5b42`→`jt200`→`jt114`→`l309c` `sx = jt1135(top)
- xbear`). The `FRUA_SKIP_ENTRY_EVENTS` J200DIFF dump (`boot.c:~10598`) records
each slot's landed x0/y0 — capture at the HEIRS 10,8,E frame, find slots with
screen X < 24. See `docs/dungeon-view-wall.md`.

### H — Combat: the spine is the keystone
The effects engine (CODE 18, **183/190**) is ready and waiting. The one function
that unblocks the most is **`l076e`** (CODE 13 per-actor turn, ~2.2KB, currently
PROBE stub at `boot.c:~33783`), lifted alongside `l4f22`→`l0434`→`l102a`→`l0116`
and wired so the encounter path actually enters `jt511` (it has no caller today).
Then the command/action/dice UI (`l1162`/`l56d8`/`l8b4`/`l4306`/`l3678`…) + a few
CODE-14 field-render entries give a minimal melee fight. Spell-capable combat
needs the CODE-16 handler tier (81 stubs) filled on top.

## Recommended order (highest leverage first)

1. **Event vocabulary (F)** — lift `l159a` → `l3118` → `l3ac6` → `l380a` → the
   chain handlers. Cheap, each live-testable on a HEIRS event cell, makes
   designed dungeons actually *play* (signage, branching, doors, inn). Best
   effort-to-payoff ratio right now.
2. **3D render placement (E)** — finish the b945821 mirror so the dungeon view is
   correct (your column symptom). Self-contained; uses the existing J200DIFF dump.
3. **Combat spine (H)** — `l076e` + the spine + wire `jt511`. The big multi-
   session unlock; the effects engine is already done, so it's orchestration +
   field render, not payloads.
4. **Rest/camp + spell memorize (I)** — the dungeon survival loop (CODE 19/21).
5. **Polish:** #129 frame-stomp (FAR-pool), `jt94` 256→16 colour (needs Mac CLUT),
   save/load slot pickers (C). Cosmetic / convenience.

## Bottom line
Boot, party-build, design-select, save/load, the dungeon walk, and the whole
**treasure/shop/vault** subsystem all work. The port is past "can it run" and
into "fill the in-game vocabulary." The two things between here and a genuinely
playable module are **(1) the event-handler vocabulary** (cheap, incremental,
testable today) and **(2) combat** (one keystone — `l076e` — then breadth). The
3D-render column bug is real but isolated (placement, not load).
