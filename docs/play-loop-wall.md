# Play-loop + event-dispatch wall — the path from "design loaded" to "adventuring"

## STATUS 2026-07-03q — the AIM-prompt corruption SOLVED: jt154 was a PROBE stub

The user's report ("the ask for the range input is corrupting the
screen"): the AIM prompt row painted OVER the stale AIM/USE/GUARD/
QUICK plates. Probes proved the l206e dirty path fired and jt176 ran —
but jt176's FRAME-piece-4 stamp is CLIPPED to the 8093..8100 band
(native y 186..200, the LOWER bar row only; faithful — the Mac clips
identically). The UPPER row's eraser is **JT[154]** (CODE 7+0x169e),
the command-menu CLOSER l0d16 runs on exit: the clipped piece-4 stamp
+ jt1193 clip reset + an UNCLIPPED FRAME piece-6 stamp (the combat
message/upper-bar plate) + l2062. The port's jt154 was an empty PROBE
stub — every combat menu close left the old bar pixels behind. Lifted
(six straight calls). VERIFIED: the AIM prompt now paints "AIM:
RANGE = 0" on clean stone, staying clean across NEXT cycling; the
"QUICK ghost" nit was the same residue and should be gone with it.

Dead-end recorded: jt94's row-23 l3f88 band erase is faithfully DEAD
CODE — it passes an already-projected Y into jt1161, which projects
again (negative -> degenerate clip -> no-op) on the Mac exactly as in
the port. Don't "fix" it. RIDER (platform/dbglog.c): dbg_file_num now
recreates DBG.LOG when the append-open fails — a mid-run host-side
delete used to silently kill all later probe output (cost this session
two blind probe rounds). OPEN NIT: the readout showed "RANGE = 0"
after cycling to a distant target — verify l2e30's cost readout value
against the Mac when convenient.

## STATUS 2026-07-03p — jt539 TARGET COMMIT verified: the melee strike lands

Follow-up to the 03o lift. Reproduced a MELEE-range test (DELAY x8 so
the spiders close): on LADY ILLIS's turn with a spider ADJACENT, AIM ->
NEXT (the info panel shows ENORMOUS SPIDER's card, and the TARGET verb
now APPEARS — r >= cost) -> TARGET. The turn advances Illis -> CLARANA,
which only happens when l302c runs jt555 and the strike ends the turn
(l26ea). So the whole jt539 commit path — pick -> l302c mode-1 strike
setup -> jt555 executor -> turn end — is LIVE for melee. The faithful
range gate re-confirmed twice: TARGET is absent for Illis's LONG SWORD
and Clarana's MACE against NON-adjacent spiders, present only when
adjacent. The CAST first-pick rides the identical jt539 path; verified
the picker's click HIT-TEST (first double-click moved the highlight
BLESS->SILENCE) but the commit->announce needs a reliable click (harness
mouse flaky) or a slow-spell QUICK path (already green from the AI side).

## STATUS 2026-07-03o — jt539 LIFTED: interactive targeting LIVE (AIM verb bar + crosshair)

The 03n card is executed. jt539 (CODE 14+0x3b6c) is a faithful full
lift: crosshair sprite bind (jt495 actor 30 -> glyph 77), weapon-range
resolve (-7262; 255 = readied item-def[12] from -27944, empty hand 1),
view centre (l6090), target ring build (l3a4e -> -7254), then the
l2e30 action-bar loop with the JT[3] arms: 0/1 NEXT/PREV ring step
(l37d6 +-1), 2 the l315e manual cursor walk, 3 commit (buf + l302c
strike in mode 1), 4 map redraw, 5 cancel; the raw arrow band 129..136
stashes the -7236 hotkey and commits at the cursor; the loop tail
re-centres and re-resolves the ring. `zero` != 0 closes with jt145.
Every helper (l2e30/l315e/l302c/l37d6/l3a4e/l6090) was already a
faithful full lift staged for this entry — jt539 was the last hole.

RIDER FIX (l2e30): the Mac passes the a8/a9 CURSOR-CELL args by
address (pea fp@27/fp@29) as jt506's walk destination — the lift had
read them as write-only scratch and passed uninitialized locals, so
`cost` was garbage and the arm-3 TARGET verb never appended.

HATARI-VERIFIED (keyboard, the AIM command): 'a' on the archer's turn
opens the AIM bar (NEXT|PREV|MANUAL|CENTER|EXIT + the range readout);
NEXT cycles to the GIANT SPIDER — the view scrolls to it and the info
panel shows its card (green name, HP, AC); the TARGET verb correctly
STAYS HIDDEN for the readied LONG SWORD vs a distant target (r=1 <
cost — the faithful range gate); MANUAL switches the bar to
CENTER|EXIT and the arrows walk the white crosshair box with the view
scrolling to follow. UNTESTED (needs a bow-readied turn or a human
mouse): the TARGET commit -> l302c -> jt555 strike, and the CAST
first-pick through the same UI. NITS: the AIM range readout paints
over the old command-bar row (present-once #144 family); white 12x12
marker residue blocks on the field (the same jt119/jt122 save/restore
family as the arrow-flight residue).

## STATUS 2026-07-03n — NEXT LIFT: jt539, the interactive targeting crosshair

The user's click test after the +20/+24 swap: the spell COMMITS (picker
closes) but no announce, no target marker — the cast exits silently.
The chain from there is fully lifted EXCEPT the last hop: jt547 ->
jt599 (instant spell) -> the -24070 callback = jt538 (lifted; jt511
installs it) -> l1efa first-pick -> **jt539 = a PROBE stub** that
always reports "no pick" -> the null-pick abort path swallows the
cast. THE SAME STUB is l08b4 case 1 (the AIM command) — it explains
the long-standing "stuck on firing the bow / targeting" awkwardness.

jt539 = JT[539], CODE 14+0x3b6c .. 0x4186 (~1.5KB): the in-combat
interactive target picker — crosshair/marker UI (l6090 paints the
marker), arrow/mouse cell walking, range gate (jt600), target
validation + NEXT/PREV cycling, returns the picked combatant in the
ua_pick10 buf and remembers it in mc[12]. Deps already lifted:
jt538/l1efa/l1dd6/l4dee (the callers), l6090, jt525/jt531, jt506,
jt173-family input. Lift it and BOTH manual cast targeting and AIM
come alive.

NOTE (user question): the Mac's CONTEXTUAL CURSOR (shield <-> sword
<-> crosshair by hover) is NOT required for targeting — jt539 draws
its own engine-side marker. The cursor swap is the jt1007/jt1123
colour-cursor pair (#107's open hook) — polish, layered on after.

## STATUS 2026-07-03m — combat CAST LIVE: the spell picker lifted (jt597/L4faa)

The "CAST not key-routed" card was a mis-diagnosis: probes showed the
routing was ALWAYS live — l0d16's accelerator pairs carry (3,'C') on a
caster's turn, l25b6 matches, l08b4 case 3 runs jt547 -> jt595. The
"nothing happens" was jt595's list builder L59c2 = a PROBE stub
returning "no spells" (AND l59c2 = jt597 per the alias table — the
alias-trap class again, this time with NO jt-side body either).

LIFTED (faithful, CODE 16): jt597 (=L59c2) arms 0 in-Memory / 5
to-Memorize / 1 in-Grimoire + the level-header tail; L5406 (the
level-sorted row insert with "(xN)" memorized counts); L4e2c (the
class/stat castability predicate over jt40); L4faa (the pick loop over
the lifted jt169, non-header row -> spell id, jt147 list free). The
scroll/scribe arms (jt597 cases 2/3/4/6/7/12 + L5726/L523e) stay TODO
stubs — camp-flow cards. Data model in the boot.c header comment:
-6454 nodes / -6450 ids / -6302 counts / -16906 defs / -17446 names /
-6864 level headers / -18893 grimoire masks.

FOLLOW-UP (user click test): rows highlighted but never COMMITTED —
the port jt169 had descriptor slots +20/+24 SWAPPED vs Mac L3600
(fp-16/fp-12): +20 must hold the jt144 CONFIRM callback that jt223
fires on a click of the already-selected row; +24 the jt168-armed
selection hook l0e92 calls (Mac L0e92 reads a0@(24) — verified).
With the stash at +20, jt223 always found 0 and no list click could
ever commit — this affected EVERY jt169 list, not just spells.
Swapped (the Mac model: click selects, click-again commits).

HATARI-VERIFIED: c on STRANILLA's turn opens the faithful picker —
"SPELLS IN MEMORY", red 1ST/2ND/3RD LEVEL headers, MAGIC MISSILE (4)
highlighted with its memorized count, arrows navigate, Return/ESC
cancel back to a clean command menu. The ROW COMMIT is mouse-first
(the verb line is just "Exit"); the harness cannot click (known
limitation) — the click -> jt547 "Begins Casting"/instant-cast hop
needs a human mouse test. jt595's remaining stub: none (l4faa/l59c2
were the last two).

## STATUS 2026-07-03l — the intermittent archer-turn crash SOLVED: InvalRect's WindowPeek cast

The 03k top card is closed, and the user's read was right — the arrow
ANIMATION was the trigger (bar flips, arrow draws, death). The chain,
pinned by Hatari `--trace cpu_exception` (bus error reading $4 at a
PC disassembling to the EmptyRect shim) + DBG probes bracketing the
death inside jt122's row loop:

  jt501 step tail -> jt122 (the 12x12 restore) -> jt1202 -> l05ea
  (mark the blitted region dirty) -> InvalRect -> compat/windows.c
  cast GetPort() to WindowPeek WITHOUT checking the port is a window
  -> `w->updateRgn` read GARBAGE past the bare screen-port struct ->
  EmptyRect(&garbage->rgnBBox) -> bus error -> GEM desktop.

WHY INTERMITTENT: the "updateRgn" slot is whatever static data follows
g_a5/qd state in memory at that moment — NULL = survive (the handle
check bailed), non-NULL = die. On the Mac this is legal code: the
screen port IS the game window (WindowRecord starts with its
GrafPort), so L05ea's InvalRect always hit a real window. The same
latent fault sat under EVERY l05ea/jt1202 blit and the jt116/jt120
InvalRect tails — combat's arrow was just the reliable trigger. It
also retro-explains the one-off "waited 0" freeze (garbage bbox that
was READABLE -> UnionRect scribble instead of a fault).

FIX (compat/windows.c): win_is_window() — validate GetPort() against
the window list before the WindowPeek cast in InvalRect/ValidRect
(+ guard the region master pointer). Faithful to the Mac contract
("the current port must be a window's port"); a non-window port
no-ops. VERIFIED: the q+z+z mid-flight-keys recipe that crashed 3/3
now survives 3/3 — arrows fly, rounds advance (NIVLOC's turn, spell
flames burning on the spiders), both verb rows intact.

Note: the mid-flight-keys correlation was a red herring (jt476's pump
is inert in combat — jt1163 is a faithful `return 0` stub); the keys
only perturbed which garbage landed in the fake updateRgn slot.

## STATUS 2026-07-03k — l5f04=jt363 resolved; the archer-turn crash is INTERMITTENT (top open card)

The stub-alias-audit's "signature clash" card is resolved: jt363 (the
STRG spell/kind-table loader) is the authoritative faithful lift; the
"l5f04 full lift" was a partial always-fail stub dead-ending the spell
tables (jt349/jt347/jt352/jt355/jt350/jt366). Six callers repointed
(details in docs/stub-alias-audit.md). Those paths are still
unreachable today: the manual CAST combat command is NOT key-routed
('c' on a caster's turn — the bar SHOWS CAST, Hatari-verified — does
nothing; a separate card), and ENCAMP memorize is untested.

BIGGER FINDING while verifying: the 03i "one-off stall" is actually an
INTERMITTENT CRASH-TO-DESKTOP on the QUICKed archer's first turn,
~1/3 of runs, and it REPRODUCES AT bd7e0c8 (before the l5f04 change,
with probes proving the repointed paths never ran). Signature: GEM
desktop garbage at frame top, the panel mid-repaint (only the weapon
line "COMPOSITE LONG BOW +1" painted — name/HP/AC rows missing), keys
dead. The healthy-run panel paints fully. So the crash fires DURING
the actor-card repaint around the arrow launch — timing-dependent
(same family as the "waited 0" freeze: one frozen run, identical
rerun sailed). Next probe pass: instrument the panel painters (jt18/
jt25/jt32/jt34/jt94) + jt501's step tail together and diff a crashed
trail against a clean one. Reproduce: FRUA_ENTRY harness, FIFO drive
p/l/a/b/c/Return, then a single q on MALTIER's turn (~1/3 hit rate).

## STATUS 2026-07-03j — treasure Slice-B LIVE: four stubs were alias twins + l2dde lifted

The 03i arrow-fix pattern generalized immediately: l3d1e's four treasure
"Slice-B stubs" were ALL alias-trap duplicates of already-lifted JT
exports (docs/lxxxx-jt-aliases.md CODE 12 row): l3b4a = jt929 (the Take
screen, wired into jt185 long ago), l1c8a = jt925 (Pool), l1d90 = jt921
(Share), l0082 = jt936 (roster-card paint). Deleted the stubs, repointed
l3d1e. The only REAL gap was l2dde (CODE 12+0x2dde), now lifted
faithfully: the stackable-loot merge over the -25302 staged list —
identity match on [40..44w,48,49,52], counts absorb into [53], fitting
nodes unlink + free (jt62), overflow pegs 255 and RETARGETS to the
leftover node. (Mac self-compares [54]/[55]/[56] against themselves — a
compiled THINK C source bug; only [54]<2 gates. Kept faithful.)

Hatari-verified end-to-end: fight -> win -> 658 XP + treasure -> T
opens the Take screen ("DAGGER +1" + the loot sprite + ITEMS/TAKE/EXIT)
-> T takes it -> the treasure screen REBUILDS its verbs (VIEW/POOL/EXIT
— Take gone, staged list empty) -> E exits to the first-person dungeon
HUD. The full play loop closes. (The 03i "leftover projectile sprite on
the XP page" was actually the LOOT ART — un-filed as a bug.)

Follow-up dedup cards (both sides are FULL lifts — compare + merge, low
urgency): l2504 vs jt926 (staged-loot probe) and l0848 vs jt934 (roster
selection; l0848 is the Hall-verified one). l2dde's merge path needs
STACKABLE loot (arrows/gems) to runtime-exercise — HEIRS spiders drop a
single dagger.

## STATUS 2026-07-03i — jt501 arrow crash SOLVED: jt502's "l1888" stub WAS jt495

The 03h crash is fixed and Hatari-verified end-to-end. Root cause: the
classic lxxxx/JT-alias trap (docs/lxxxx-jt-aliases.md line: `l1888 =
jt495`). jt502 (JT[502], CODE 13+0x2b2c — the missile-glyph binder run
by l2b24's type-30 arrow arm) was lifted against a fresh PROBE stub
`l1888`, but L1888 IS the already-lifted jt495 (the boot.c jt495 header
even says "= L1888"). So the arrow sprite was never copied into TILE-
registry slot 77, and jt501's first l19a0 blit read the EMPTY slot ->
bus error -> GEM desktop. Fix = delete the stub, repoint jt502's three
calls to jt495 (arg-for-arg identical; the seq/negb commit flag is 1).

Suspect #1 from 03h (the -27866 source) was DISPROVEN by the Mac asm:
jt501 itself pushes a5@(-27866) at 0x1d5c/0x2174, and l4d98 composes
that registry at boot (l36e0(81) + 2x23 COMSPR jt111 loads, slots
15..37/53..75); combat binds 76..80 per-shot via jt495/jt497/jt502.
The port jt501/l19a0/jt111/jt119/jt122 bodies all check out faithful
against CODE 13 0x1a24-0x21ea (read end-to-end this session).

VERIFIED (FRUA_ENTRY level-10 harness, FIFO drive, probe trail then a
clean build): arrows fly the full Bresenham walk ("J501 final/out",
5+ complete l2b24 flights), rounds advance through the whole party,
spiders close + attack lines resolve ("STRANILLA ATTACKS GIANT SPIDER
AND MISSES"), the party WINS -> "CONTINUE BATTLE?" -> 658 XP + "THE
PARTY HAS FOUND TREASURE!" -> the full treasure screen (illustration,
roster, VIEW/TAKE/POOL/EXIT) — the previously-untested Slice-B renders.

Leftovers spotted (small, separate cards):
- Intermittent: one probe run stalled after step-0's jt476 return
  (trail ended "waited 0"; the identical rerun sailed through). Keys
  pressed DURING a flight get eaten by jt476's jt1067 idle pump —
  likely the same class as the old "QUICK stall". Watch for it.
- The verb bar can paint "QUICK" twice (highlight ghost at the bar's
  right edge) during auto-rounds.
- The XP/treasure page shows a leftover projectile sprite mid-page
  (the jt122 restore skipped when the fight ends mid-walk residue).

## STATUS 2026-07-03h — the QUICK "round-freeze" = a CRASH in jt501 (archer's arrow)

USER INSIGHT cracked it: the garbled boxes at the top of the frozen frames
are the GEM DESKTOP (icons + window) drawn over our Videl mode — frua had
CRASHED to the desktop (bombs -> Pterm), and every "ignored key" was typed
at a corpse. Not a wait, not a loop: a death.

The kill chain, probe-verified (three marker rounds, each converging):
  QUICKed ARCHER's turn -> l5b9a keeps the remembered target (jt554 ok)
  -> attack line up (B f10=1) -> jt555 -> prelude ok (jt476(100) completes)
  -> b != 0 (the arrow item) -> l2b24 -> it[40]=30 -> the type-30 arm
  (jt502 + jt52(13)) completes -> "M preanim" logs -> jt501(trajectory)
  -> CRASH inside jt501, before post501.

jt501 suspects (in likelihood order):
1. l19a0(sx, sy, 77+anim, 5, g_a5_long(-27866)) — the -27866 TILE
   registry as the projectile sprite source: check whether -27866 is
   composed in COMBAT (it's a dungeon-walk composition; DUNGCOM/COMSPR
   own combat art) and whether the Mac's L2b24/L19a0 really source it.
2. The jt119/jt122 save/restore under the projectile (12x12) at
   l1944/l1972-mapped coords — combat-field vs area-map coord spaces.
3. Phase-1's buf148 fill is bounded only by l6f68 termination (max-span
   lines write exactly 148 — the edge; verify the combat grid is 50x25).
NEXT: probe jt501's phases (post-phase1 count, post-jt521, pre/post
l19a0 per step, log -27866) OR read Mac L1a92-ish (jt501, CODE 13) and
compare the sprite-source argument directly.

ALSO from this session's probes: jt541's round-1 seeds are mostly 0 for
the webbed party (hdr[46] slow gate = the spiderweb event -> faithful),
and the -27928 list holds ~20 nodes (6 party + 2 spiders + backup-copy
nodes seeding mc[4]=0 out-of-combat — harmless but worth a dedup look).

## STATUS 2026-07-03b — DIAGNOSIS CORRECTED: no hang — combat is fully functional

The precision markers (jt555 P1..P5 / l14bc / T2..T4 + l302c post) show
COMPLETE attack cycles resolving repeatedly, and a liveness screenshot
caught the "stall" red-handed: an active MOVE/ATTACK mode ("MOVE LEFT =
10") for a badly wounded actor (8 HP, cyan) with the camera scrolled to
his corridor. The perceived hangs were INPUT WAITS: move mode consumes
only arrows/UNDO/DONE and silently repolls everything else, and with
the camera scrolled away from the party the state reads as frozen. The
earlier "bow-card wait" was the same class. Real damage flows both
ways; attacks, saves, ammo, poses, XP and the win path all execute.

WHAT REMAINS ON #115 (all normal cards, no mystery):
- Natural fight-end needs DIRECTED play (walk fighters into spiders) —
  blind q-grinding mostly ends in move-mode waits. FRUA_AUTOWIN covers
  automated end-to-end testing.
- l5b9a's no-attack-line passes (-22628 stays 0 for ~19 iterations —
  jt554 LOS / l713c reach worth a look; makes AI turns slow).
- The garbled CPIC portrait strip (art decode).
- Treasure Slice-B interiors (need a loot-dropping design to test).
- The l4af4 double placement pass.

## STATUS 2026-07-03 — the ranged-turn wait NARROWED to the attack-resolution chain

Two probe rounds after the mc[0] fix. The trail: l5b9a loops ~19 passes
with -22628=0 (no attack line established -> the iter-guard bails to
l6042/l6454), then a pass reaches the hit: run A hung INSIDE jt555
(entered, never returned — before the jt476(100) beat); run B passed
straight through jt555 (in/A/B/l14bc/C all fired, via the l302c
single-target dispatch, NOT l5b9a's L5fe8 site) and hung AFTER it in
the unprobed caller tail. So the hang MOVES with the dice but lives in
the attack-resolution chain: jt555's prelude / l14bc's swing loop /
l302c's tail. NEXT SESSION: probe l14bc's interior (the per-swing
loop) + l302c after-jt555 + jt523/l6836's animation waits; also note
l5b9a spends most passes with NO attack line (-22628 stays 0 — jt554
LOS failing? the l713c reach test?) which is why fights crawl.

The visible state at the wait: bow-actor card + the garbled CPIC
target strip, keys dead, pump alive (dwell-style).

## STATUS 2026-07-02m — the CAST hang SOLVED (aa97fbd): mc[0] byte-vs-word gates

The jt599(0) probe run pinned it: BOTH forced-action gates (l08b4 +
l5008) read *(short *)(mc+0) where the Mac reads the zero-extended BYTE
mc[0] (L08b4 0x8d6 / L5008 0x5168 `moveb a0@`). The word read also saw
mc[1] — the can-cast flag jt541 now seeds — so every caster with no
queued spell fired the arm as jt599(0), whose bogus effect-0 announce
hung. jt541's lift exposed the latent width bug. Byte gates fixed; the
delayed-cast model now runs: jt547 stores the spell in mc[0] ("Begins
Casting" + initiative re-queue) and the next round's forced arm casts
it — the FIRST live CODE 16 payload run (a real spell fired; the
known-garbled CPIC target strip painted).

NEXT WAIT STATE (the new natural-end blocker): the AI archer's RANGED
target pick — bow card + target strip up, no key consumption (Return/
ESC dead). Suspects: l5b9a's missile arm target loop or the l1714/aim
path reached with an AI actor. Probe l5b9a's ranged branch next.

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

## 03s — the render-family pair: opaque text plates + the walk-loop clip leak (2026-07-03)

Two API-level render fixes, both user-flagged, both with broad reach:

**1. jt1089 now paints the text plate UNCONDITIONALLY (the overprint fix).**
Ground truth: the Mac's colour-mode glyph writer is L4fae (CODE 4; JT[1136]'s
flush takes it whenever -2347 != 0 — L4e12's Erase/Paint dither waterfall is
the 1-BIT build only). L4fae, disasm-verified: RGBForeColor(palette[HIGH
nibble]) + RGBBackColor(palette[LOW]) -> _PaintRect(cell) -> TextMode(3,
srcBic) -> _DrawString. The colour word is (PLATE<<4)|TEXT and the cell is
ALWAYS filled — there is NO transparent text in this renderer. The port's
"transparent by default, plate only for (bg==15||fg==0)&&bg!=8" special case
left stale glyphs under every in-place redraw: the rest-editor digits, GAME
SPEED, round counters — the whole overprint family. Verified: main menu
unchanged, rest editor crisp through value+field changes, SPEED digit crisp.

**2. The l63c0 walk loop leaked the clock-box clip (the pink camp backdrop).**
Two sites set jt1173(8024,8092,8058,8156) (projected clip LEFT = 184) without
the jt1193 restore that the balanced entry site has. Every walk tick left the
clip active, so ANY screen composed straight from the dungeon had x<184
clipped out — the camp backdrop blit (l534a) dropped entirely, showing stale
page bytes (the pink noise). Probe-proven: clip left 184 on the garbled pass,
0 on the correct pass, same group/handle/args. Both sites now restore
(jt1193) after their clipped op. Verified: the campfire renders at Encamp
entry, dungeon walk unaffected. NOTE: the spiderweb-cell re-fire corruption
seen while verifying is the KNOWN pre-existing "bigpic stomps wall pool on
walk-back re-fire" (#115 memory), not this change.

## 03t — the walk-back event-pic garble: state + repro recipe (2026-07-03)

The #115 "re-fire corrupts the 3D view" item, partially diagnosed:
- The event pic path is l08ce -> l541a("PIC", ev[6]) -> l534a; the load runs
  jt131(2), and jt131 LEAVING mode 0 runs the stage-4 wall unbind (jt209(0) ->
  l11ca reclaim) — so the designed dispose EXISTS on this path. The garble is
  therefore NOT a missing unbind hook; suspicion moves to (a) -31234 not being
  0 at re-fire time (unbind skipped), or (b) second-cycle FAR-pool state (the
  garble ONLY reproduced on the SECOND play cycle of one boot).
- REPRO RECIPE (confirmed once): boot -> p l a b -> c -> Return x2 (AUTOWIN
  build) -> ENCAMP -> EXIT (to menu) -> p l a b -> c -> Return x2 -> turn left
  + step forward = the spiderweb event re-fires with the viewport shredded
  (green herringbone = wall-tile bytes). A FRESH boot's same walk shows a
  clean dungeon and no re-fire garble.
- Probes to plant next session: jt131 (old,new) transitions + l541a's l33ac
  handle + the 'glib: Out of FAR memory!' path, during the second cycle.
- NOTE: multi-cycle scripted drives are fragile (the second p/l/a/b desynced
  into the menu's Art Gallery once); add generous settles or screenshot-gate
  each step.

## 03u — plate-grey correction: jt1089 was missing the JT[1006] remap (2026-07-03)

User-caught regression from 03s: the new text plates painted RAW clut 8 — a
LIGHTER grey than the panels, which route their fill through jt1006 (the
-4188 colour-range table) when -1312 (the 8-bit play state) is set. The Mac's
L4fae remaps BOTH nibbles through JT[1006] (0x5028..0x505e, gated on -1312) —
the port's jt1089 had skipped that step for both the plate AND the glyphs.
Fixed: jt1089 now remaps fg + plate via jt1006 under the same gate. Verified:
plates blend invisibly into the panels (no light bars), the campfire entry
and the crisp rest-editor digits both hold.

## 03v — the give-treasure flow LIVE end to end (2026-07-03)

The "dead app after the caravan reward" was FIVE NULL derefs of rec[64] (the
member's combat sub-record — NULL outside combat; the Mac reads low memory
harmlessly, the Falcon bus-errors — the InvalRect class again): three in
l33d8's party sweeps, one in l4046's disband loop (both reached by jt930 from
the non-combat give-treasure path), and two pre-emptively in the combat-setup
placement sites. All guarded with mc != NULL.

Hatari-verified END TO END on the HEIRS front door: intro caravan -> the
give-treasure event -> "EACH CHARACTER RECEIVES 100 EXPERIENCE POINTS / THE
PARTY HAS FOUND TREASURE!" -> the treasure-pile art + VIEW|TAKE|POOL|SHARE|
EXIT -> TAKE -> MONEY -> "PLATINUM 100" -> "HOW MUCH PLATINUM WILL YOU TAKE?"
-> 100 taken -> "THERE IS STILL TREASURE LEFT..." (the ring stays) -> the
chained farewell pages ("may the gods watch over you", the Thirsty Traveler)
-> the DUNGEON with a live command bar. The mode-flip concern was unfounded —
the walk loop handles the case-3 refresh fine once the crashes are gone.

## 03w — the "second-cycle garble" DIAGNOSED: it's the entry-compose gap, both cycles (2026-07-03)

Probe-driven correction of 03t — the second-cycle framing was WRONG:

1. The l709e dispatch is IDENTICAL on both cycles (probe: event 33 type 10 ->
   event 34 type 1, l694e gate 1 both times). The fired-bit/save-reload theory
   is dead.
2. Event 33 is a TYPE-10 encounter prompt (l3b0e: "the passageway is choked" +
   CUT WEB / BURN WEB / ...). Type 10/21 arms draw NO picture (no l442e).
3. The garble is the VIEWPORT UNDER that pic-less prompt: the prompt fires at
   play-entry BEFORE any play-screen compose reaches the VISIBLE page. On a
   fresh boot the stale page is BLACK (cycle 1 — probe-confirmed, the same
   prompt renders on pure black: the "clean" look nobody flagged); on re-entry
   it's shredded transition remains (the "cycle-2 garble"). Same bug, uglier
   stale bytes.
4. A qd_present() pair before l709e(special) does NOT fix cycle 1 — the
   chunky buffer itself is black at that point: jt23's mode-4 entry compose
   PARTIALLY no-ops on the first entry (jt937 roster text + jt938 clock land;
   l67ca's FRAME chrome blits and the jt103 panel fills do NOT). Cycle 2's
   compose paints the chrome fine (g5_e) — an asymmetry that points at the
   modal page stack (-2352/-2354) or the FRAME group residency at that phase.
5. VERDICT: this is #144 (compose-once/present-once) + the entry-compose
   ordering, not an event bug. Acceptance test for #144: the type-10 entry
   prompt at the AUTOWIN harness cell must show a coherent play screen behind
   it on BOTH a fresh boot and a re-entry.

Repro/harness notes: the entry event fires at 'b' (BEGIN) — gate screenshots
BEFORE pressing 'c' (which answers the prompt as CUT WEB). The Hall drive
letters: l = LOAD SAVED GAME, a = save A pick + Hall refresh, b = BEGIN.

## 03x — #144 investigated: the double-buffer model is CORRECT; the entry black is PALETTE (2026-07-03)

Deep probe of the entry-black (the "second-cycle garble" from 03w). The #144
premise — "off-screen compose, present once, faithful jt1146/jt1153 double-
buffer" — is the WRONG target for this bug. Findings, all measured:

1. The chunky surface is SINGLE and persistent (compat/quickdraw's screen
   port baseAddr == platform/display_videl's g_surface.pixels; qd_screen_pixels
   returns exactly what videl_present LUT-blits). #144's "double-buffer at the
   page-descriptor level" doesn't exist — jt1146/jt1153 already collapse to the
   HAL present. The 16bpp triple-buffer is display-side only and full-frame.
2. During the entry prompt, qd_present is called ~1770x (every idle-pump
   iteration), and the viewport-region checksum of g_surface is IDENTICAL on
   every one (938619) — the composed content IS in the surface AND IS being
   presented, frame-stably. So "present once" is a PERF optimisation (stop
   presenting 1770x for one idle screen), NOT a correctness fix.
3. The pixels ARE there but map to BLACK: viewport row-60 sample =
   {0,0,146,147,148,147,0,0} (structured, a centred element), and the CLUT
   probe shows clut[40]=0 and clut[146] flips 0->5516 across frames. The
   FRAME-chrome / backdrop palette is UNCOMMITTED (or committed late) during
   the entry-event compose.
4. ROOT: the entry-event path (l4b56 -> l709e(special) -> the type-10 l3b0e
   prompt) composes via jt23 case 4 (l67ca chrome + jt937/jt938 text) but does
   NOT run the port's port_draw_play_frame / play-palette commit that the WALK
   LOOP's compose (jt312 region) does — so CLUT 16..31 (FRAME) and the backdrop
   range never load before the prompt shows. Text renders (jt1089/jt1161 remap
   through jt1006 onto committed slots); raw chrome/backdrop indices (40, 146)
   hit uncommitted CLUT = black.

RECLASSIFY: this is a #125-family PALETTE-commit gap on dungeon entry, not
#144. The real #144 (present-once perf) is independent and low-priority. The
FIX for the black: commit the play-screen palette (the FRAME 16..31 range +
the backdrop range) in the entry compose before the landing-cell event fires
— i.e. route the pre-event compose through the same palette path the walk
loop uses, or call port_draw_play_frame / jt85 at l4b56 before l709e(special).
Speculative fixes tried and reverted (none addressed the palette): boot clip
seed, jt1193-at-entry, forced qd_present after l709e.
