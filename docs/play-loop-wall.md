# Play-loop + event-dispatch wall — the path from "design loaded" to "adventuring"

## STATUS 2026-07-02l — the mid-fight stall NAMED: the AI spell-cast payload (CODE 16)

Post-jt182-fix retest: QUICK turns now flow end to end — l0d16 pick ->
l1842 AI-flip -> l5008 -> l6454 setup -> l5b9a ATTACK with its jt476
animation beats (100/20x15/400) -> next actor. The remaining natural-end
blocker is ONE reproducible state: when the AI mage (STRANILLA) takes a
forced CAST turn, the card paints "CASTS A SPELL", jt476(1000) dwells,
then the flow disappears into jt599's spell PAYLOAD (CODE 16) and never
returns — no key consumption (Enter/ESC dead), no probed function hit.
The CODE 16 handlers are breadth-first lifted and runtime-untested
(docs/code16-wall.md); this is their first live exercise failing.
NEXT CARD: probe jt599's dispatch (which effect id) + the handler's
interior; suspects = the target/aim loop or the l6114 shared core.
Until then FRUA_AUTOWIN covers fight-end testing.

## STATUS 2026-07-02k — full combat loop CLOSES (e53cd1c): XP/treasure + jt182 fix

The XP/treasure chain (0ab0d8d: l33d8+l2fd4+l31e0+l3806_c12+l3d1e+l2504)
is lifted, and a new FRUA_AUTOWIN harness (off by default; kills the
monster side at jt511 entry) let it run end to end. Two things landed
exercising it:

- **jt182 fast-path bug**: the "standard Press-Return prompt?" gate
  compared p2 against the g_a5_14644 MACRO (the slot ADDRESS) where the
  Mac + jt148 use g_a5_long(-14644) (the cached prompt-string pointer).
  l3806_c12 passes that pointer as p2, so the Mac takes the fast l1806
  path (the "PRESS [RETURN] TO CONTINUE." button, shortcut 64); the
  port fell to the l206e modal whose button had a 'P' shortcut nothing
  dismissed -> the XP page hung. One-line fix (match jt148).

**Hatari-verified end to end**: walk -> web event -> CUT WEB -> spider
battle -> (AUTOWIN) win -> "THE PARTY HAS WON. EACH CHARACTER RECEIVES
548 EXPERIENCE POINTS. THE PARTY HAS FOUND NO TREASURE." -> Enter
dismisses -> back at the dungeon (full roster/clock/command bar). Plain
build: the live spider fight is unchanged.

**make gotcha logged**: EXTRA_CFLAGS changes alone don't retrigger a
recompile — `touch src/engine/boot.c` when flipping harness defines.

**Remaining #115**: the treasure Slice-B interiors (l3b4a Take/Pool/
Exit, l1c8a, l1d90, l0082 card paint, l2dde stack-merge) fire only when
a monster drops loot (HEIRS spiders drop none, so untested); the l4af4
double placement pass; and the mid-fight QUICK stall (the fight doesn't
end on its own without AUTOWIN — the auto-turns don't resolve).

## STATUS 2026-07-02j — jt1125 EVENT-MASK fix (2c0e1f9): phantom input SOLVED

The "QUICK hang", "'g' -> ATTACK ALLY?" and spurious strip commits were ONE
bug: jt1125 ignored its Toolbox event MASK. Engine sites pass kind = 7
(null+mouseDown+mouseUp) — the Mac's jt1125 never returns KEYS there (keys
go via the L725c/jt1133 pending stamp); the port returned them as events
and L2d3e fed (ascii, modifiers) into its (mouse_y, mouse_x) HOVER
hit-test -> every keypress committed the movement strip under the parked
pointer (l25b6 mode-4, argc 16 -> key 133 -> l1162 move -> ally confirm).
Fix: honour the mask (keys need bits 8|32; mouseDown bit 2); the -818
pending stamp stays. VERIFIED: 'g' Guards via the phase-5 DLItem shortcut,
turns advance, the spider AI closes distance, and the mage's "CASTS A
SPELL" announcement paints. FAITHFULNESS NOTE: QUICK (arm 11) is a
ONE-SHOT l26ea on the Mac; the persistent-AI toggle is arm 6 (l1842 sets
+383) — the old "Flee" label on that arm is wrong, the code is right.
Remaining #115 cards: l33d8/l3806_c12/l3d1e (XP + treasure), the l4af4
double placement pass, jt930's full-lift follow-ups.

## STATUS 2026-07-02i — COMBAT TURNS LIVE (98d2d59): jt541 + jt27

The "l5008/l08b4 interiors" pass found the whole action tier ALREADY
LIFTED (l1162/l5b9a/l6454/l6c96 + the CODE 13 spine — see
docs/code13-wall.md, 97% done). The no-op rounds were ONE leaf:
**jt541 (CODE 14+0x0006, per-round prep) was a PROBE stub** — every
actor's action count mc[4] stayed 0, l076e skipped every turn. Lifted
jt541 (full: spell-scan -> mc[1], door predicate, jt543/jt868-18/l1090
chain, mc[4] = 1d6 + jt27 dex mod, the hdr[46] slow-side -6 gate) +
jt27 (CODE 6+0x1c92, the dex band -4..+5).

**Hatari-verified**: verb bar (DELAY/VIEW/SPEED/END + AIM/USE/GUARD/
QUICK), active-actor highlight + info card, MOVE/ATTACK with live
movement points + camera scroll, ATTACK ALLY? confirm, and REAL DAMAGE
(77->70 HP, wounded-cyan repaint) from the spider counterattack.

**FOLLOW-UPS**: (1) the active-cell highlight paints OVER the actor
sprite (blank gray box — the pose/erase order in jt530/jt868 sel-7?);
(2) 'g' inside move mode routed into the attack-ally confirm (key-map
audit of l1162's JT[1] table); (3) jt930 post-fight rewards; (4) jt63
count->string stub (combat log lines); (5) the l4af4 double placement
pass. Combat is now PLAYABLE end-to-end for the first time.

## STATUS 2026-07-02h — EMPTY BATTLE SOLVED (60c7e63): jt127 .glb fallback + jt125 + l3c24

The 2026-07-02g instrumentation plan ran and found the real roots — NOT
jt542/l4f22/l0434 (all lifted + correct). Two stubs upstream:

1. **jt127's .glb fallback was the deferred stub** (`*out = 0`, buffer
   untouched). Stock monsters live in MONST.GLB (HEIRS.DSN only ships
   custom MONST101/102/108/109), so the spawn's jt588 -> jt127("MONST",
   11) parsed UNINITIALIZED STACK as a compressed record: garbage
   side/status bytes -> jt490 counted 0 -> jt511 exited instantly (the
   "empty battle" + the garbled portrait strip), or — memory-layout
   depending — jt1171 ran wild -> bus error -> TOS bombs -> Pterm(-1)
   (the "stuck PRESS RETURN modal" = a DEAD APP under a stale Videl
   frame; diagnosed via `--trace gemdos` showing the Fopen MONST011.dat
   double-fail then Pterm at ROM PC). Lifted the faithful Mac tail
   (CODE 6 0x1d6..0x2ce) + **JT[125]** (CODE 6 0xa0, the item-extract
   callback, full lift): "<prefix>.glb" via jt987, item num through
   jt1013/jt1011/jt401 and the -31250/-31248/-31244/-31246 A5 block;
   jt464 group-19 FC cache hit path; -31235 gates jt467 commit vs
   jt462 rollback.

2. **l3c24 (jt53's measure leaf) was a 0/0 stub** -> l0d2a rec[130] =
   (0*2+0-2)&0xff = 254 -> body class 254&7 = **6**, past the 6-row
   -27540 footprint table (rows 0..5 only — offset 72 IS -27468, the
   id-table count; verified vs the expanded DATA image via
   tools/datapool.py) -> l5d92 garbage steps -> jt515 probed off-map
   (y=39!) -> feat==0 latch -> l41b2 rejected every cell -> ALL spawned
   monsters dropped at l4af4's just-summoned arm. Full lift over
   jt1005/jt1139 (both already lifted).

**Hatari-verified**: spiders (types 17+64) spawn, PLACE on the combat
field opposite the party in real DUNGCOM art, the round loop runs 15
rounds at side counts 6/4, "CONTINUE BATTLE? YES/NO" + NO -> clean
teardown -> walk loop (clock ticks 12:00->12:15 AM). Corrected stale
"PROBE stub" claims: l490c/l3f24/l404e/l4af4 are all LIFTED.

**NEXT (#115)**: the rounds are no-ops — the per-actor turn handlers
are the last tier: **l5008 (CODE 13, monster AI turn)** and **l08b4
(CODE 13, player action dispatch)** + the 13-command JT[3] dispatcher
interiors. Also follow-ups: the l4af4 placement runs TWICE per battle
(investigate the second pass), and the combat-entry harness keys need
the FIFO (`hatari-event keypress <ascii>` / `keydown 28`+`keyup 28`
for Return — XTEST Return does NOT reach SDL on a real :0 display).

## STATUS 2026-07-02g — jt512 = faithful rts; jt511 RECON complete (lift-ready)

**jt512 (CODE 14+0x5d8e) is a bare `rts` on the Mac** — the port stub is
the faithful lift (marked). The whole combat-prep tier therefore reduces
to jt511 itself.

**CORRECTION (same day): jt511 IS ALREADY FULLY LIFTED** (boot.c ~43540,
the CODE-16-campaign era) and matches the disasm decode exactly — round
loop, jt490(=L242c) flush, jt541 prep, l0434 initiative build, the
-22624[idx] walk with l076e per actor, side-count/morale end conditions,
key polls, l102a/l0116/l0006 teardown. Its local family (l4f22, l0434,
l076e + l08b4 action dispatch, l102a, l0116) is lifted too. The
"pass-through fight" therefore means the loop EXITS IMMEDIATELY at
runtime: with monsters now spawning (473059c), instrument after l4f22 +
l0434 — are the SIDE COUNTS -25298/-25297 populated, and does -22620/
-22624 hold the initiative list? Whichever is empty names the next lift
(suspects: jt542 CODE 14+0x5434 setup, l4f22's counting pass, l0434's
sort — check each body's depth). The map below stands as reference:

**jt511 (CODE 13 0x5a6..0x1886, ~4.8KB) — the combat main loop, mapped:**
- Head: -27990 = 5 (combat mode), -24070 = &JT[538] (method install).
- JT[3] @0x94c min0 max12 — the 13-arm per-turn COMMAND dispatcher
  (the combat verb menu). Case offsets 0x96c/988/9ae/9d8/9ea/a48/a5a/
  a8e/b18/b38/b5c/b64/b76, default 0xcd2.
- JT[1] @0xba4 — 13 keys, cursor codes 129..136 ALL -> one shared arm
  0xbdc (the movement funnel; engine arrow codes minus 128).
- JT[1] @0x126a — 9 keys: 27(ESC) + 129..136 (the aim/target mode).
- JT[1] @0x17e6 — 27/264/260 (ESC + Up/Down; a list scroll).
- JT[3] @0x1814 min0 max1 — a small binary arm near the tail.
- Callee inventory (per-port grep): jt155 (x13 — the hot one), jt531,
  jt525, jt42, jt868, jt530, jt40, jt521✓, jt166, jt879, jt545, jt543,
  jt532, jt526, jt519, jt476, jt38, jt176✓, jt173(missing), jt67
  (missing) + locals L26ea (x8), L283e, L242c. All but two exist.
Level-2 structural skeleton is the right scope: mirror the CFG, call
the JTs in order, defer the 13 command arms' interiors to follow-ups.

## STATUS 2026-07-02e — L1176 NPC-ally spawn lifted (7f75a9c); entry-walls OOM flake

l1176 is a FULL lift (ally template rec[94]=9/rec[147]|=50, group of 10,
cap 60, portrait at the bumped -22311 — see the commit). NEW FLAKE to fix
FIRST next session: the level-10 ENTRY intermittently paints backdrop-only
(starfield, no walls) + "glib: Out of FAR memory!" — 2 of 3 post-l4cc0-
bucket runs. The 450K pool's entry set (3 per-set walls ~111K + BACK.CTL
backdrop + event bigpic ~165K + level-5-load residue from the save) sits
at the edge; the ~32K of new bucket HEAP may also have killed the 320K cw
fallback buffer's headroom (lazily NewPtr'd only when the pool path fails
— both failing = no walls at all). Diagnose with the conout OOM log +
a pool-contents dump (l11ca's record walk); candidate fixes: release the
LOAD-time level-5 binder groups at the jt198 level switch, or accept a
larger-than-Mac pool on 4MB (the 1MB-floor goal is stage-5 work anyway).
The combat chain verified end-to-end in 473059c's build; l1176 only runs
inside the combat entry and is orthogonal to the flake.

## STATUS 2026-07-02d — COMBAT RUNS END-TO-END (473059c); next tier = jt511/jt512 internals

The stuck modal was three missing lifts deep, all landed:
1. **l13e8 FULL** — the DLItem shortcut matcher's JT[1] arms (42 '*' any,
   35 '#' CR/ESC, 64 '@' CR/LF). The prompt button's code-64 shortcut now
   matches Return; every jt175/jt453 "PRESS RETURN" modal is key-dismissable
   (they never were — mouse-only until now).
2. **l0006_20 FULL** (was PROBE) — the play-entry combat init; seeds the
   monster-slot high-water -22311 = 8. Includes l41fa (LE word store —
   the game record's word fields are little-endian on disk).
3. **l4cc0 buckets** — L30f4(60) 398B monster records / L317c(70) /
   L31a4(68) + two L531e scratch blocks (were deferred; spawns got NULL).

PROVEN in Hatari: web prompt → CUT WEB → spider page → Return → "A battle
begins..." → 4 monsters spawn (jt588/l0d2a, deep-copied chains) →
jt510/jt512/jt511 → jt920/jt930 → back in the walk loop, playable.

**NEXT #115 TIER — make the fight real:** jt511's CODE 13 main-loop
internals (the combat screen, initiative, per-monster turns) and jt512's
CODE 14 map prep are pass-throughs. Also queue: l1176 (NPC-join, CODE 20
0x1176, still PROBE — GEO010 hdr[262]=11 so it's on this path), jt45/jt49
(CODE 6 0x5700/0x5730 CPIC binder reset — tiny, disasm read), jt930
rewards. NITS seen post-combat: roster panel overdrawn by the level-title
text (recompose); the post-combat message shows the chained MARK/text
event oddly.

## STATUS 2026-07-02c — per-set wall loads LANDED (b4fa8f7); one modal from combat

The binder stall is FIXED, faithfully: l33ac's >4-char early-out removed (the
jt987+jt104 fallback extracts ONLY the requested ~37K per-set sub-GLIB — the
Mac model, confirmed by the disasm + BEOWOLF's 8x8d1NNN.TLB overrides);
l6eea's digit arm un-inverted (colour modes = PER-GROUP names, Mac CODE 7
0x6f1c); cw_wallfile_load keeps whole-file semantics via a plain name. HEIRS
level 10 now runs the WHOLE encounter chain: spider-web prompt (all 5
choices) → CUT WEB → l673e branch → the giant-spider combat page, in colour,
no OOM. LAST BLOCKER: that page's "PRESS RETURN TO CONTINUE" modal never
consumes the key (either input mode) — the continue-poll between l159a's
text page and the L18e2 combat entry. Find WHICH modal paints it (l1806? a
jt182 wait? or l159a's own poll) and why its key path differs from the
caravan pages (which dismiss fine). The lifted spawn chain (jt588 et al)
waits right behind it. Repro: harness build → P L A B → c → Return.

## STATUS 2026-07-02b — spawn chain LIFTED (62ebaaa); the level-10 stall is the BINDER wall path

The four spawn-chain lifts landed (jt588 / l0cc6_c20 / l0d2a_c20 / l10a0 —
see the commit for the per-function notes), plus two real bug fixes found
by driving the combat cell:
1. **l3b0e read the prompt id big-endian** — the Mac assembles it byte-wise
   LITTLE-endian ((ev[9]<<8)+ev[8], asm 0x3b70); id 39 became 10239.
2. **cw_wallfile_load OOM'd on two-wall-file levels** — level 10 mixes
   8X8DB (sets 1/2) + 8X8DC (set 10), the sibling group stayed bound so
   the orphans-only l11ca couldn't reclaim it; fixed with the stage-4
   sibling dispose. Level 10's 3D view now RENDERS.

**REMAINING STALL (the next lift): the -27894 BINDER wall path.** After
the first compose, the flow stalls before l709e/l63c0 (probed): the
faithful jt114 blit chain (l6eea binders → jt468/l37aa re-resolve per
blit) has the SAME two-file contention — with a 450K pool, re-binding
8X8DB evicts 8X8DC and vice-versa PER BLIT (a quasi-infinite compose,
frame pixel-frozen, no exception, one "Out of FAR memory!" logged).
Options: (a) caller-driven dispose at the binder level with per-SET
sub-loads (the Mac's l33ac spec "8x8d%c1" + the per-set 8x8dNN files
seen in BEOWOLF.DSN suggest the Mac loads per-set SUB-libraries, not the
whole 296K file — investigate first); (b) pool-size the two files.
NOTE: the spawn chain is runtime-unverified until this lands — the
harness repro is one command (see above), and DBG probes at l63c0/l709e/
the combat entry pinpoint progress instantly.

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
