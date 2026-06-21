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
| F | **Dungeon EVENTS** (`l709e`, 39 arms) | 🟡 | **9 handlers lifted** (text `l4d26` 2/14, give-treasure `l28b0` 3/25, stairs `l5676` 5/11/34, shop `l5586` 8, temple `l216a` 9, vault `l3a32` 24, combat-prompt `l3b0e`/`l673e` 10/21). **~23 still STUB.** |
| G | **Treasure / shops / vault** | ✅ | **Slice B complete** this session: the picker UI (`jt183`/`jt185`/`jt929`/`jt921`/`jt922`/`jt924`/`l2ebc`) + the 3 triggers (shop/temple/vault) + vault file I/O + the bucket-init fixes that make the caravan reward (100pp + ring) actually land. |
| H | **Combat** | 🔴 | Effects engine (CODE 18) **96% done** — but the SPINE is stubbed (`l076e` per-actor turn + `l4f22`/`l0434`/`l102a`/`l0116`); `jt511` loop is lifted but has **no live caller**; field render (CODE 14) + 81 CODE-16 handlers gated on the spine. Multi-session. |
| I | **Rest / camp** (CODE 19), **spell memorize** (CODE 21), **inventory** (CODE 9) | 🔴 | Mostly stubs — the dungeon "survival" loop. |
| J | **Editor / design tools** (CODE 2/11/22) | ⏸ | Deferred by ADR-0008 (runtime first). |

## The frontier in detail

### F — Event-handler vocabulary (the cheapest high-value work)
Now that the walk fires `l709e` per-cell, each handler is **independently
liftable AND live-testable**. Highest value STUBs (ranked):
1. **`l159a`** (cases 1 & 33) — plain text/message events. The single most common
   event type in any module; lifts the bulk of designed signage/narration. Small.
2. **`l3118`** (case 36) — Yes/No **QUESTION** event (branch on ev[8]/ev[9]).
   `l3bee` insert already lifted; this is the predicate/dialog. Gates non-linear design.
3. **`l3ac6`** (case 17) — **SECRET DOOR** (pairs with the Search command).
4. **`l380a`** (case 13) — **INN** (rest/heal civic event).
5. **Chain handlers** `l4f9a`/`l1ad8`/`l3cd6`+`l3328`/`l364e`/`l29cc` (7,15,18–20),
   `l6436` (35) — compute a next-event index; until lifted, scripted multi-step
   events dead-end after their first arm.
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
