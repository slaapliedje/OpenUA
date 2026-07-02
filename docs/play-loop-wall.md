# Play-loop + event-dispatch wall — the path from "design loaded" to "adventuring"

## STATUS 2026-07-02 — #115 frontier LOCATED: the combat entry hangs at its setup stubs

Drove a real type-10 combat headless via the NEW test harness (build
`make EXTRA_CFLAGS="-DFRUA_ENTRY_LEVEL=10 -DFRUA_ENTRY_COL=4 -DFRUA_ENTRY_ROW=12
-DFRUA_ENTRY_FACING=2"` → Load save A → Begin Adventuring lands ON HEIRS
level 10's combat cell (4,12), event #33; the override hooks sit in l0bbc,
off by default). Findings:

1. **The encounter-prompt slice is DONE and user-visible.** l3b0e + BOTH
   choice renderers (l026e_c20 / l03f6) are fully lifted (the l3b0e header
   comment claiming "still stubs" was stale — fixed). Verified: HEIRS
   level 1's entry event renders the keep bigpic + "DOES THE PARTY ENTER
   THE TOWN?" with a working YES/NO choice bar.
2. **The combat ENTRY reaches the exec tier and HANGS on its setup stubs.**
   Landing on the combat cell paints the play chrome then freezes: the
   l159a combat arm runs l10a0(ev) [PROBE stub — the monster-group/roster
   builder from the event], l1176() [PROBE stub], jt510 (faithful rts),
   jt512() [PROBE stub — CODE 14 combat prep], then jt511 (the CODE 13
   main loop, level-2) spins with nothing set up. "A battle begins..."
   never paints. **NEXT LIFTS — the spawn chain, scoped bottom-up
   (2026-07-02b, disasm read):**
   1. **jt588** (CODE 15+0xd9c) — MISSING: the monster-record fill (the
      thing that turns a design monster id into a live 398-byte record).
   2. **L0cc6** (CODE 20 0xcc6..0xd2a, ~100B) — record alloc: jt477 node
      from the party bucket + jt588 fill; returns the node + copies its
      [8]/[4] list heads to the caller's outs; flag clears rec[95].
   3. **L0d2a** (CODE 20 0xd2a..0x109c, ~275 asm lines) — the per-group
      SPAWNER: guards the 50-monster cap (-22266), loops `count` times
      calling L0cc6, difficulty-scales HP/thac0-ish bytes rec[395]/
      rec[129] by the game record's -28006[39] (only when rec[95]==1 and
      0 < [39] < 6), bumps -22267 for rec[90] specials, per-slot flags.
   4. **L10a0** (decoded, trivial) — the 6-slot group loop: for each
      non-empty -4917[i] slot, flag = ev[10] & {0x20,0x40,0x80} for
      slots 3/4/5, call L0d2a(type, count(-4911[i]), type, i+8, flag).
   5. **L1176** (CODE 20 0x1176..0x1472, ~760B) — the NPC-join: gated on
      ds[262] != 0 (HEIRS level 10 = 11, so it IS on the repro path);
      L0cc6-spawns the NPC record (rec[94]=9, rec[147]|=50) and splices
      it into the party list.
   6. **jt512** (CODE 14) — combat map prep.
   7. Then jt511's stubbed heavy locals + jt930 (rewards) as they
      surface.
3. **Event-pic CLUT clobber, worst case found:** the level-1 keep bigpic's
   palette floods the whole UI band GREEN (panels, backdrop) — far worse
   than the caravan's subtle case. The known "pic palette clobbers UI clut
   0..31" bug (bigpic-composer notes) now has a stark repro:
   FRUA_ENTRY_LEVEL=1, Begin Adventuring.
4. **Wilderness/area levels (HEIRS 1-4, HDR wall-slots 0xff) are not
   walkable**: after the town prompt, the area screen shows empty green
   panels with an ENCAMP bar, and arrows fall through to the main menu.
   The area-mode movement/render (jt953 mode-4 arm, jt501/jt521 composer
   wiring) is its own gap.

GEO event decode recipe (offline): FORM+16 chunk walk → ENCR = 100 × 20-byte
events (ev[0]=type; combat=10/21), MAP cell = (col*H+row)*6, bytes 4/5 =
event id / zone; HDR bytes 4..9 = wall-set slots (0xff = area level),
entries at HDR+12. Combat events in HEIRS: L1 ev#11 @(13,9) [area], L10
ev#33 @(4,12), L12 ev#23/#31, L15 ev#17, L17 ev#45 [all 3D].

The keystone subsystem (from the roadmap): what stands between a loaded design
and an interactively-playable dungeon. **Corrected finding 2026-06-19: the loop
is ~80% wired — `l709e` (the 39-case event dispatcher) is fully LIFTED and the
walk moves the party. The gaps are narrower and clearer than "the play loop is
missing."** Update status columns in the same commit as each lift.

## ✅ THE WALK RUNS (2026-06-19) — dungeon loads + party moves + turns

Both upstream blockers are cleared; the dungeon is interactively walkable in
**HEIRS.DSN** (stage it — `make gamedata DSN=HEIRS.DSN`; TUTORIAL has only
open 1-cell rooms with nowhere to walk). The fix was two commits:

1. **(0) The dungeon LOADS** — `Begin Adventuring` aborted with `glib: Out of
   FAR memory!` because the ~296KB wall lib + ~165KB event bigpic need ~461KB
   resident at once, over the Mac's 450KB FAR cap. Raised the cap to 768KB
   (`master_init(...,214,768)` at `ua_main`; jt463 negotiates down to free RAM,
   safe on 4MB). `cf1ecfd`. HEIRS now renders the full dungeon HUD — 3D corridor,
   roster, compass, command bar.
2. **The walk DISPATCHES + ADVANCES** — `jt297` was reachable once the dungeon
   loaded (`l63c0` runs, arrow keys route through `l2d3e`→case 0→`jt297` with
   the right 257..264 codes). The move logic was already correct (verified live:
   forward col 0→1, right-turn facing 2→4, mirrored into rec+46). The view just
   didn't follow: `jt297` faithfully RESTORES the view globals `-12288..` to the
   pre-move cell (the Mac's deferred smooth-scroll `L4900/L423e/L3998` is what
   walks the view up to the party cell). That animation isn't lifted, so the new
   position lived only in rec+46 and the pinned view never moved. Fix: snap the
   view cell to the rec+46 mirror in `l63c0`'s re-render (hard jump for the
   missing scroll). `afc6b98`. Verified: forward steps walk down the corridor
   past doors, turns rotate the first-person view.

**Gap 1 (per-step `l709e`) is now REACHABLE** — `jt297` runs the trigger on each
new cell. Still un-verified that a designed interior event cell *fires* (most
handlers are stubs; needs a known HEIRS event-cell coordinate to step onto).

Known-still-broken (separate from the walk, NOT regressed): the **3D wall-piece
decode** (`dungeon-3d-render-state` — the "Escher" geometry; HEIRS renders a
coherent-enough corridor to navigate) and the **compass face** (doesn't rotate
with facing). The **position HUD reads 0,0** (`jt938` bug, `band4-campaign`).

## The chain (what runs, in order)

```
l07dc  →  jt918 Training Hall  →  "Begin Adventuring" (case 10 / l115a)
       →  jt948  (dungeon loop, CODE 20+0x4a12 — level-2 SKELETON)
            ├─ on AREA ENTRY:  jt201(x,y) → l709e(special)   ✅ WIRED (line 3490)
            └─ inner loop L4be8:
                 ├─ dungeon (level>=5):  jt240 → l63c0 (unified walk+command)
                 │     ├─ move keys → jt297 → l1908 → jt312 re-render   ✅ moves
                 │     └─ command-bar latch g_walk_cmd (0..7)
                 │          → View(3)/Inv(7) ✅ ; Move/Area/Cast/Search/Look ⛔ TODO
                 └─ overland (level<5):  jt953 command dispatch         ✅ lifted
       →  l709e(idx)  (CODE 20+0x709e, jt947) — the 39-CASE EVENT DISPATCH  ✅ LIFTED
            → reads ev = -13038 + (idx-1)*20  (event table ✅ loaded from design)
            → switch(ev[0]) → one of 39 event-type HANDLERS  ⛔ ~35 are STUBS
```

## The THREE gaps (in dependency order)

### Gap 1 — the per-STEP event trigger (the keystone wiring)  ⛔
`jt948`'s entry arm fires `l709e` once on area entry (line 3490), but the
**walk loop (`jt240`/`l63c0`) moves the party WITHOUT firing `l709e` on the new
cell** (line 3523: *"the per-command menu dispatch is the next wiring step"*).
So an event placed on an interior cell — shop, combat, text, teleport — **never
triggers while walking.** This is a SMALL but critical wiring fix: after a move
commits the new `-12288`/`-12287` position, read the cell special (`jt201`, as
`l085e` already does → `-12284`) and dispatch it (`l709e`). Without it, nothing
below matters; with it, every lifted event type starts working. (Faithful spot:
the Mac fires the landing-cell event inside the move handler / `l1908` tail.)

### Gap 2 — the event-type HANDLERS (the FRUA event vocabulary)  ⛔
`l709e`'s switch is complete, but ~35 of the 39 handlers are PROBE stubs. Each
is a self-contained, independently-liftable FRUA event type. **Now TESTABLE
once Gap 1 lands** (step on a designed event cell → handler fires).

| type | handler | status | what (FRUA event) |
|-----:|---------|--------|-------------------|
| 2 | `l4d26` | **LIFTED** (1.3KB) | (text / display) |
| 5, 11, 34 | `l5676` | **LIFTED** (3.4KB) | stairs / transfer-module / teleport |
| 10, 21 | `l3b0e`/`l673e` | **LIFTED** (#115) | combat / special encounter |
| 36 | `l3118` (+branch) | stub | **Question** (Yes/No → ev[8]/ev[9] branch) |
| 8 | `l5586` | **LIFTED** 2026-06-20 | **Shop / merchant** ("local shop" / "May I help you?") -> jt183 |
| 13, 32 | `l380a` / `l38bc` | stub | **Inn / Vault** ("enters a local Inn" / "the vault") |
| 17 | `l3ac6` | stub | **Secret door** ("discovered a secret door") |
| 1, 33 | `l159a` | stub | text/message variant |
| 3, 25 | `l28b0` | stub | (give/take?) |
| 4 | `l1f76` | stub | |
| 6 | `l2d32` | stub | |
| 7 | `l4f9a` | stub | chain-to-event (sets idx=ret) |
| 9 | `l216a` | stub | conditional chain (idx=ev[12]) |
| 12 | `l2e42` | stub | |
| 13 | `l380a` | stub | |
| 15 | `l1ad8` | stub | chain |
| 16 | `l6020` | stub | |
| 18,19,20 | `l3cd6`+`l3328`/`l364e`/`l29cc` | stub | chain-by-computed-index |
| 22 | `l5bde` | stub | (uses gameRecord[133]) |
| 24 | `l3a32` | stub | mode-save event |
| 26 | `l2b2a` | stub | |
| 27 | `l5fcc` | stub | |
| 29 | `l398a` | stub | |
| 35 | `l6436` | stub | chain |
| 37 | `l661c` | stub | |
| 38 | `l66cc` | stub | |
| 23 | (inline) | done | chain-to ev[8] |
| 0,28,30,31 | — | n/a | no-op |

(Handler→event-type names are partly inferred from strings; confirm each at lift
time against the FRUA editor's event list.)

### Gap 3 — the dungeon COMMAND actions  ⛔
In the walk loop's command dispatch (line 3543): View(3)/Inventory(7) are
lifted; **Move/Area/Cast/Search/Look are TODO**. `Search` (find secret
doors/traps) and `Cast` (non-combat spells) are the visible ones. Plus `jt948`'s
**level-transition arms** (the `[133]`/`[134]`/`[49]` stair-direction + level
scroll at 0x4ad6..0x4be4) are skeleton/TODO.

## Priority — to an interactively-playable dungeon

1. **Gap 1: the per-step event trigger** — tiny, and it lights up combat
   (already lifted) + every event type you lift after. Do this FIRST; verify by
   walking the party onto a combat cell in a loaded design.
2. **Gap 2: the common event handlers** — text (`l159a`), shop (`l5586`), give/
   take items, teleport (done via `l5676`), question (`l3118`), vault (`l380a`).
   Each is small + self-contained + now testable.
3. **Gap 3: Search + Cast commands** + `jt948` stair transitions — polish the
   exploration verbs.

**Correction to `docs/roadmap.md`:** the play loop is NOT a from-scratch gate —
`l709e` + the walk + the event table are lifted. The real keystone is **Gap 1
(one wiring fix)**; after it, the work is the event-handler vocabulary (Gap 2),
each independently liftable and testable. Tasks #115 (combat) + #124 (walk) live
here.
