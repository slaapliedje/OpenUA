/* autoplay_script.h — the headless auto-drive KEY SCRIPT, shared by every
 * platform's input backend.
 *
 * ★ THIS FILE EXISTS BECAUSE THE SCRIPT WAS DUPLICATED AND THE COPIES DIVERGED.
 * platform/input.c (Atari) and platform/amiga/input_amiga.c each carried their
 * own g_ap[]. Both copies even carried a comment warning that the other one
 * existed — and it still did not work: the FRUA_AUTOWALK_INN route added to the
 * Atari copy on 2026-07-31 fired nothing on the Amiga, which silently went on
 * running the old 18-step wander and finished at cell (11,7) reading "GED OUD!"
 * instead of at the tavern. Two captures were spent chasing that as a modal
 * -chain bug. A comment is not a mechanism; one definition is.
 *
 * Include it from inside each backend's `#ifdef FRUA_AUTOPLAY` block. The
 * backend keeps its own ap_due()/ap_take() (they differ in how they log), but
 * the SCRIPT — the thing whose correctness is per-run and per-machine — has
 * exactly one definition.
 *
 * Scan/ascii pairs are TOS codes on every target: the Amiga backend translates
 * its rawkeys into the same codes before the engine sees them, so a single
 * table serves both.
 */
#ifndef FRUA_AUTOPLAY_SCRIPT_H
#define FRUA_AUTOPLAY_SCRIPT_H

struct ap_key { unsigned char scan, ascii; unsigned short delay; };
static const struct ap_key g_ap[] = {
	{ 0x19, 'p',  600 },    /* Play the Game -> Training Hall (10s)  */
	{ 0x1E, 'a',  600 },    /* Add Character -> seeded roster list   */
	{ 0x50, 0,    300 },    /* Down -> give the list focus + select BARBARUS */
	{ 0x1C, 0x0D, 600 },    /* Return -> add the selected (* BARBARUS)   */
	{ 0x01, 0x1B, 600 },    /* Escape -> back to the hall            */
	{ 0x30, 'b',  900 },    /* Begin Adventuring -> dungeon (15s art)*/
#ifndef FRUA_CBTKEYDIAG
	/* #62 diag runs drop the nudge keys: they queue past the CBTAUTO fire
	 * and get consumed as combat moves, polluting the command-read test. */
	{ 0x4D, 0,    300 },    /* Right (nudge: force the 3D paint)     */
	{ 0x4B, 0,    300 },    /* Left  (net-zero facing)               */
#endif
#ifdef FRUA_AUTOWALK
	/* #61 walk soak. The base script TURNS but never WALKS, so the walk phase
	 * has never been sampled headlessly: external keys get dropped when a
	 * screenshot steals focus (that is what defeated the 2026-07-24 soak), and
	 * autoplay — the one injector immune to focus loss — had no forward steps.
	 *
	 * The walk is the interesting case for the page-flip suspects, because a
	 * step is the ONE path that presents a rect instead of a full frame:
	 * st_present_rect draws the SHOWN page in place and does not flip, and a
	 * rect inside the viewport skips the c2p entirely for st_vp_composite.
	 * Turn-then-step-then-turn re-renders the viewport from several facings so
	 * a stale-page or clipped-rect artefact has repeated chances to show.
	 *
	 * Generous delays: a full recompose is ~2s of emulated 8MHz, and a step
	 * that queues faster than it drains just merges frames and hides the very
	 * artefact we are hunting. Never ships (release_guard has no opinion on it
	 * because FRUA_AUTOPLAY already gates the whole array).
	 *
	 * ★ THE RETURNS ARE LOAD-BEARING — do not drop them. HEIRS fires a MODAL
	 * event chain (the Skull Crag caravan: several messages, each waiting on
	 * "PRESS RETURN TO CONTINUE") the moment the party enters the dungeon. Walk
	 * keys sent into that modal are simply eaten, so the first version of this
	 * block fired all ten keys and walked NOWHERE — the soak looked like it had
	 * covered the walk and had not. Caught only by noticing the last frame of a
	 * 110-grab AGA run was still the event screen. Clear the chain first. */
	{ 0x1C, 0x0D, 360 },    /* Return — dismiss entry event 1        */
	{ 0x1C, 0x0D, 360 },    /* Return — 2                            */
	{ 0x1C, 0x0D, 360 },    /* Return — 3                            */
	{ 0x1C, 0x0D, 360 },    /* Return — 4                            */
	{ 0x1C, 0x0D, 360 },    /* Return — 5 (spare: chain length varies)*/
	{ 0x1C, 0x0D, 420 },    /* Return — 6 (spare)                    */
#ifdef FRUA_AUTOWALK_LONGINTRO
	/* Real modules open with a STORY CHAIN, not one message: BEOWOLF and
	 * GIANTS were both still on "PRESS RETURN TO CONTINUE" after the six
	 * Returns above, so every movement key that followed was eaten and the
	 * walk sampled nothing (the same trap the six Returns were added for,
	 * one size up). Ten more clear a longer intro. Only for real modules —
	 * on the event-free WALKTEST room these would land in the walk view. */
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 360 },
#endif
#ifdef FRUA_AUTOWALK_TREASURE
	/* ★ A CHAIN THAT ENDS ON THE TREASURE SCREEN NEEDS TWO NON-RETURN KEYS.
	 * HEIRS' Skull Crag caravan hands the party a hoard, and the treasure
	 * screen (VIEW TAKE POOL SHARE EXIT) ignores Return completely: sixteen
	 * of them left the 2026-07-27 drive parked on it with every walk key
	 * eaten — the same trap the Returns themselves exist for, one level
	 * further in, and invisible because the key count still read 34 of 34.
	 * EXIT, then answer NO to "THERE IS STILL TREASURE LEFT. DO YOU WANT TO
	 * GO BACK AND CLAIM IT?", then clear the caravan-farewell messages that
	 * follow it.
	 *
	 * SEPARATE from LONGINTRO deliberately: 'e' is ENCAMP on the walk command
	 * bar, so firing this at a module whose entry chain hands out no treasure
	 * opens the camp screen and the walk samples that instead. Pass it only
	 * for a module that actually needs it (HEIRS does).
	 *
	 * Verified live on STE 2026-07-27: reaches the walk bar at 10,8 12:00 AM,
	 * and the following steps move 10,8 -> 11,8 -> 12,8 with the viewport
	 * changing at each one. */
	{ 0x12, 'e',  600 },    /* EXIT the treasure screen              */
	{ 0x31, 'n',  600 },    /* NO — do not go back and claim the rest */
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 }, { 0x1C, 0x0D, 300 },
	{ 0x1C, 0x0D, 420 },
#endif
#ifdef FRUA_AUTOWALK_CMDS
	/* #131: the commands the force-full comment names as the hazard. CAST is
	 * an EMPTY switch arm that does nothing, and that is the point: it exits
	 * the walk loop, l63c0 is re-entered through jt948 -> jt240, and jt221's
	 * chrome prelude re-lays bare FRAME pieces. The chrome repaint used to
	 * cover them. Drive it explicitly, because no autowalk event chain does. */
	{ 0x2E, 'c',  420 },    /* CAST                                  */
	{ 0x01, 0x1B, 420 },    /* Escape                                */
	{ 0x2F, 'v',  420 },    /* VIEW                                  */
	{ 0x01, 0x1B, 420 },    /* Escape                                */
	{ 0x17, 'i',  420 },    /* INV                                   */
	{ 0x01, 0x1B, 420 },    /* Escape                                */
#endif
#ifdef FRUA_AUTOWALK_INN
	/* #142: the DEMO route, for the side-by-side capture. The soak below wants
	 * many steps from many facings; a demo wants ONE legible destination, so
	 * this replaces it rather than adding to it.
	 *
	 * HEIRS drops the party at GEO005 (10,8) facing SOUTH, which is where the
	 * caravan hands over the hoard. The farewell text points at the inn by
	 * name — "'THE THIRSTY TRAVELER' FOR A BITE TO EAT AND SOME DRINK" — and
	 * its common room is the Tavern event on cell (8,9): two cells NORTH and
	 * one EAST, with the inn's own sign (ev[8], "THE THIRSTY TRAVELER.") on
	 * (7,8) one further north. So: turn about, walk up to the door, turn in.
	 *
	 * The route was read off GEO005 rather than guessed — cell_special() is
	 * event index + 1, and (8,9) reads 64 = ev[63] "set standard rumors",
	 * which chains to ev[41], the Tavern. Nothing else in the area opens one.
	 *
	 * ★ YOU CANNOT WALK IN FROM (8,8). The obvious approach — up the street and
	 * turn right at the door — is a WALL: the cell's four edge bytes are
	 * ordered [W, S, E, N], and (8,9) reads W=15, S=15, E=0, N=0, so the common
	 * room opens only east and north. The first capture ran straight into that
	 * and sat facing a blank wooden wall at (8,8) for the last ten seconds.
	 * Go one further north to (7,8) — the cell under the inn's own sign — then
	 * east and down through the top door.
	 *
	 * (That edge order is not in docs/geo-format.md's cell table, which says
	 * only "the four edge walls". It is settled, not guessed: pairing each
	 * cell's edge with its neighbour's opposite edge across all 342 vertical
	 * edges of GEO005 gives 0 disagreements for [W,S,E,N] and 111 for the
	 * N/S swap.)
	 *
	 * ★ 0x4B is the LEFT arrow and it turns the party EAST from north — the
	 * arrows are clockwise/anticlockwise, not compass points. Confirmed twice:
	 * the RIGHT arrow walked the party west to (8,7), and the LEFT arrow put it
	 * nose-first into (8,8)'s east wall. Read the position readout, not the key
	 * name.
	 *
	 * Halved by AP_DELAY like everything from index 8 on, so these read as
	 * twice what they are: 30 ticks (0.5 s) per move, 600 (10 s) to sit on the
	 * tavern at the end. Deliberately short — a walk step is THE number the
	 * capture exists to show, so the script must not be the thing pacing it. */
	/* Drain first. Everything below is RELATIVE — the two Lefts turn the party
	 * about, and every step after them assumes that worked. A message box
	 * still up when the first Left arrives swallows it (an arrow key dismisses
	 * nothing), the party never turns, and the route walks off in the opposite
	 * direction. Returns that reach an empty walk bar do nothing, while one
	 * Return too few silently redirects the whole route, so over-draining is
	 * the cheap side of the trade.
	 *
	 * ★ HONESTY NOTE ON WHY THIS BLOCK EXISTS. It was added to fix the AGA
	 * capture finishing at (11,7) reading "GED OUD!" instead of at the tavern,
	 * on the theory that the Amiga's entry chain ran a modal longer. That
	 * theory was WRONG and the block changed nothing: the AGA was running a
	 * DIFFERENT SCRIPT ENTIRELY, out of a second copy of g_ap[] that lived in
	 * platform/amiga/input_amiga.c (see this file's header comment). The
	 * reasoning above is sound, but it is not the fix for anything that has
	 * actually been observed, and nobody should read it as evidence that the
	 * chain length varies.
	 *
	 * TRIMMED from twelve Returns to two, because the insurance was NOT free
	 * after all: on a fast machine the surplus Returns arrive at an already
	 * empty walk bar, and the movement keys behind them queue up against
	 * whatever is still open. On the Falcon capture that reads as the party
	 * refusing to move, a burst of error beeps, and then several steps at
	 * once — which looks exactly like an engine bug and is not one. Two
	 * spares cover a chain that runs one message long; more than that buys
	 * noise. */
	{ 0x1C, 0x0D,  40 }, { 0x1C, 0x0D, 120 },
	{ 0x4B, 0,     60 },    /* Left  — turn about (1/2)              */
	{ 0x4B, 0,     60 },    /* Left  — now facing NORTH              */
	{ 0x48, 0,     60 },    /* Up    — (10,8) -> (9,8)               */
	{ 0x48, 0,     60 },    /* Up    — (9,8)  -> (8,8)               */
	{ 0x48, 0,     60 },    /* Up    — (8,8)  -> (7,8), under the sign */
	{ 0x1C, 0x0D,  60 },    /* Return — spare: (7,8) is ev[8], "THE
	                         * THIRSTY TRAVELER.", and it may speak. A
	                         * Return the walk bar does not need is
	                         * harmless; one it needs and lacks eats
	                         * every movement key after it. */
	{ 0x4B, 0,     60 },    /* Left  — face EAST                     */
	{ 0x1C, 0x0D,  60 },    /* Return — spare, same reason            */
	{ 0x48, 0,     60 },    /* Up    — (7,8)  -> (7,9)               */
	{ 0x4B, 0,     60 },    /* Left  — face SOUTH, the door below    */
	{ 0x48, 0,   1200 },    /* Up    — into (8,9): the tavern greets */
#else
	{ 0x48, 0,    420 },    /* Up    — step 1                        */
	{ 0x48, 0,    420 },    /* Up    — step 2                        */
	{ 0x4D, 0,    360 },    /* Right — turn, forces a fresh viewport */
	{ 0x48, 0,    420 },    /* Up    — step 3 (new facing)           */
	{ 0x4B, 0,    360 },    /* Left  — turn back                     */
	{ 0x48, 0,    420 },    /* Up    — step 4                        */
	{ 0x4B, 0,    360 },    /* Left  — turn                          */
	{ 0x48, 0,    420 },    /* Up    — step 5                        */
	{ 0x4D, 0,    360 },    /* Right — turn                          */
	{ 0x48, 0,    420 },    /* Up    — step 6                        */
	/* #96: six steps yield exactly EIGHT rect presents, which is one
	 * b63play window — and that window's `wall` starts at the FIRST rect
	 * present, so it spans the whole modal intro and reports the walk's
	 * display share against a wall that is mostly not walking. Steps 7..18
	 * exist purely so the SECOND and THIRD dumps are pure walk. */
	{ 0x4B, 0,    360 }, { 0x48, 0,    420 },   /* 7  */
	{ 0x4D, 0,    360 }, { 0x48, 0,    420 },   /* 8  */
	{ 0x48, 0,    420 },                        /* 9  */
	{ 0x4B, 0,    360 }, { 0x48, 0,    420 },   /* 10 */
	{ 0x4D, 0,    360 }, { 0x48, 0,    420 },   /* 11 */
	{ 0x48, 0,    420 },                        /* 12 */
	{ 0x4B, 0,    360 }, { 0x48, 0,    420 },   /* 13 */
	{ 0x4D, 0,    360 }, { 0x48, 0,    420 },   /* 14 */
	{ 0x48, 0,    420 },                        /* 15 */
	{ 0x4B, 0,    360 }, { 0x48, 0,    420 },   /* 16 */
	{ 0x4D, 0,    360 }, { 0x48, 0,    420 },   /* 17 */
	{ 0x48, 0,    420 },                        /* 18 */
#endif /* !FRUA_AUTOWALK_INN */
#endif
};
#define AP_N ((short)(sizeof g_ap / sizeof g_ap[0]))
/*
 * The delays above are sized for a SOAK: each one is long enough that the
 * slowest target has finished redrawing before the next key lands, which makes
 * every machine take about the same wall-clock time. That is exactly wrong for
 * a capture meant to SHOW the speed difference — a 0.15 s Falcon step and a
 * 0.92 s STE step both vanish into a 7 s gap and the two videos look identical.
 *
 * So the demo shortens them, per phase: the menu/hall keys (0..7) keep their
 * full delays, the modal event chain (8..) halves, and the walk route at the
 * end sets its own short delays literally.
 *
 * The menu/hall phase is exempt because it is the one phase that must be
 * DETERMINISTIC rather than fast, and quartering it was not. Two captures with
 * identical key scripts seated different parties — MALTIER alone on one run,
 * BARBARUS and LADY ILLIS on the next — because the Add-Character list is
 * still being built when the keys arrive, and a Down that lands early is
 * dropped while a Return that lands late is taken twice. A different party is
 * a different roster to paint, which is exactly the kind of difference these
 * videos are supposed to be free of. Nothing interesting is being timed here
 * anyway; the menu is a static screen.
 *
 * The chain gets the gentler cut because it is the one phase where a
 * permanently-pending key is a real hazard — it polls BETWEEN messages as well
 * as during them, and a Return consumed by one of those polls dismisses
 * nothing, so the spare Returns can be burned without advancing. The walk bar,
 * by contrast, polls only when idle, so there a pending key is simply the next
 * step: a player holding the arrow down, which is exactly what the capture
 * wants to time.
 *
 * ★ WHAT THIS SHORTENING IS NOT RESPONSIBLE FOR — the 2026-07-31 capture ran
 * the whole script and ended in the dungeon EDITOR (unmistakable: PLACE WALL,
 * WD 19 HT 19). The delays were the obvious suspect and were WRONG: rebuilding
 * with the proven unscaled delays reproduced it exactly. The variable that
 * actually flips it is Hatari's FAST-FORWARD, which hatari_ui.sh leaves ON for
 * the boot and drops at the menu marker — with it off from launch the same
 * scaled script walks the route correctly. Capture runs must pass
 * `--fast-forward no`, which they want anyway for real-time video.
 */
#ifdef FRUA_AUTOWALK_INN
#define AP_MENU_KEYS 8
#define AP_DELAY(i, d) ((unsigned long)((i) < AP_MENU_KEYS ? (d) : (d) / 2u))
#else
#define AP_DELAY(i, d) ((void)(i), (unsigned long)(d))
#endif
#endif /* FRUA_AUTOPLAY_SCRIPT_H */
