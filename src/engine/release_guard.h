/* release_guard.h — a shipping build must not carry a debug flag that CHANGES
 * BEHAVIOUR.
 *
 * Several FRUA_* flags are not diagnostics: they alter what the game does.
 * FRUA_AUTOWIN silently kills the monster side at combat entry.
 * FRUA_SKIP_ENTRY_EVENTS skips the landing-cell event chain. FRUA_CORRIDOR and
 * FRUA_RAYCAST swap in a different 3D renderer. FRUA_SHIM_DEMO runs the Toolbox
 * demo instead of the game.
 *
 * Any of those in a release would be a silent, hard-to-spot behavioural lie —
 * a build that plays a subtly different game. "We'll remember not to" is not a
 * mechanism, so `make release` defines FRUA_RELEASE and this header turns the
 * mistake into a compile error.
 *
 * Pure diagnostics (FRUA_*TRACE, FRUA_CELLSCAN, FRUA_ENGINE_PROBE, ...) are NOT
 * listed: they only add output, so they are merely noisy in a release, not
 * wrong. Add a flag here when it changes behaviour, not when it changes volume.
 */
#ifndef FRUA_RELEASE_GUARD_H
#define FRUA_RELEASE_GUARD_H

#ifdef FRUA_RELEASE

#ifdef FRUA_AUTOWIN
#error "FRUA_AUTOWIN in a RELEASE build: combat would auto-resolve. Never ship this."
#endif
#ifdef FRUA_SKIP_ENTRY_EVENTS
#error "FRUA_SKIP_ENTRY_EVENTS in a RELEASE build: cell events would not fire."
#endif
#ifdef FRUA_CORRIDOR
#error "FRUA_CORRIDOR in a RELEASE build: that is the fallback renderer, not the faithful one."
#endif
#ifdef FRUA_RAYCAST
#error "FRUA_RAYCAST in a RELEASE build: that is the fallback renderer, not the faithful one."
#endif
#ifdef FRUA_SHIM_DEMO
#error "FRUA_SHIM_DEMO in a RELEASE build: that runs the Toolbox demo, not the game."
#endif
/* Added 2026-07-25. These three had been shipping-unguarded; each alters what
 * the game does, so each belongs here by this header's own rule. */
#ifdef FRUA_RNGSEED
#error "FRUA_RNGSEED in a RELEASE build: the RNG would be pinned, so every player gets identical dice, initiative and encounter rolls forever. It exists only for the deterministic A/B harness (docs/deterministic-ab.md)."
#endif
#ifdef FRUA_HALLFREE
#error "FRUA_HALLFREE in a RELEASE build: training would be free and would ignore the XP requirement (it raises the Mac's own -22730 bypass)."
#endif
#ifdef FRUA_PARTYHP
#error "FRUA_PARTYHP in a RELEASE build: every party member's HP would be clamped at combat entry. It exists only to drive a fight to a deterministic death (docs/deterministic-ab.md)."
#endif
#ifdef FRUA_NOINK
#error "FRUA_NOINK in a RELEASE build: the new-ink re-quant trigger would never fire, so a stamped ink the quantiser never saw would ride the luma fallback (invisible text after a re-band). It is the #63 cost-ablation switch."
#endif
#ifdef FRUA_R3DEXTENT
#error "FRUA_R3DEXTENT in a RELEASE build: it snapshots and re-compares the whole 64000-byte surface around every 3D render, which is slower than the render. It is the #63 write-extent probe."
#endif
#ifdef FRUA_DIRTYCHECK
#error "FRUA_DIRTYCHECK in a RELEASE build: it re-scans every row the dirty set said to skip, which is the whole cost the dirty set exists to avoid. It is the validator for #63's narrowed scan, not a shipping mode."
#endif
#ifdef FRUA_QDT_NOGRAB
#error "FRUA_QDT_NOGRAB in a RELEASE build: a direct writer's pointer grab would no longer mark the surface dirty, so any screen painted only through a grabbed pointer could present STALE. It exists to size the dirty-row prize for #63 (docs/planar-plan.md)."
#endif
#ifdef FRUA_CBTPLAY
#error "FRUA_CBTPLAY in a RELEASE build: the party would play its own combat turns, ignoring the player. It is the headless combat auto-turn harness (#74)."
#endif
/* Added 2026-07-26, found while working #61. FRUA_AUTOPLAY had been shipping
 * unguarded despite being the MOST intrusive harness flag of the set: it types
 * for the player from the moment the main menu appears. FRUA_CBTPLAY, which
 * only takes over combat turns, was already listed — so this was an omission,
 * not a judgement call. (FRUA_AUTOWALK merely appends walk steps to the same
 * array, which lives entirely inside #ifdef FRUA_AUTOPLAY, so it is inert on
 * its own and this one #error covers both.) */
#ifdef FRUA_AUTOPLAY
#error "FRUA_AUTOPLAY in a RELEASE build: the engine would inject its own keystrokes from the main menu onward, playing itself past the player. It is the headless drive-into-the-dungeon harness (platform/input.c g_ap[])."
#endif
#if defined(FRUA_ENTRY_LEVEL) || defined(FRUA_ENTRY_ROW) \
 || defined(FRUA_ENTRY_COL) || defined(FRUA_ENTRY_FACING)
#error "FRUA_ENTRY_* in a RELEASE build: the party would be teleported to a hard-coded cell on every play entry, ignoring the design's start area."
#endif
/* Added 2026-07-26 with the #48 BLiTTER measurement. Not a gameplay change, but
 * it stalls st_init for several seconds running its copy loops and grabs 128 KB
 * of ST-RAM to do it — on an 8 MHz machine that reads as a hung boot. */
#ifdef FRUA_NOSOUND
#error "FRUA_NOSOUND in a RELEASE build: the game would be SILENT. It exists only to price the software synth against the rest of the play loop (#63)."
#endif
#ifdef FRUA_SNDNOGATE
#error "FRUA_SNDNOGATE in a RELEASE build: the synth would go back to rendering ~410 samples of silence every vblank whether or not anything is audible — measured at 2.5x the whole ST/STe play loop (#96). Output is identical, which is exactly why this must never ship silently: it is the #96 A/B arm, not a mode."
#endif
#ifdef FRUA_TRAPPX
#error "FRUA_TRAPPX in a RELEASE build: the dungeon view's three SOLID perspective regions would go back to a bounds-checked store per pixel instead of a memset per row — measured at 24-28 of a walk step's ~89 ticks against 0-3 (#133). Pixels are identical (screen-hash sequences match compose for compose), which is exactly why it must never ship silently: it is the A/B arm, not a mode."
#endif
#ifdef FRUA_BACKDROPDIV
#error "FRUA_BACKDROPDIV in a RELEASE build: the dungeon backdrop would go back to a 32-bit DIVIDE PER PIXEL for its horizontal scale — ~1,100 cycles into libgcc, 7,744 times per walk step on an 88x88 viewport, measured at 53 of a step's ~128 ticks against 14 with the column table (#132). Pixels are identical (screen-hash sequences match compose for compose), which is exactly why it must never ship silently: it is the A/B arm, not a mode."
#endif
#ifdef FRUA_MODALFORCEFULL
#error "FRUA_MODALFORCEFULL in a RELEASE build: every event message would go back to a FULL play-screen recompose — measured at 580/708/555 ticks against 301/430/277 for the HUD-only path, i.e. ~4.6 s of extra wait per message on an 8 MHz STE (#131), to repaint a clock digit (#130). Screens are identical (screen-hash sequences match compose for compose), which is exactly why it must never ship silently: it is the A/B arm, not a mode."
#endif
#ifdef FRUA_AUTOWALK_CMDS
#error "FRUA_AUTOWALK_CMDS in a RELEASE build: it appends CAST/VIEW/INV keystrokes to the headless autoplay script. Test-only (#131)."
#endif
#ifdef FRUA_PERPIXELBRIDGE
#error "FRUA_PERPIXELBRIDGE in a RELEASE build: every chunky->planar bridge (qd_planar_bridge_rect, dc_plane_bridge_span) would go back to a per-pixel read-modify-write of all four planes plus per-pixel coverage bookkeeping — measured at ~450 cycles/pixel and 57% of l67ca, MORE than the decode+blit it mirrors (#126). Output is byte-identical, which is exactly why it must never ship silently: it is the A/B arm, not a mode."
#endif
#ifdef FRUA_PERPIXELFILL
#error "FRUA_PERPIXELFILL in a RELEASE build: every solid fill would go back to a per-pixel read-modify-write of all four planes — measured at 96% of jt103's cost, 4.85 s of a full recompose on an 8 MHz STE against 0.97 s (#125e). Output is byte-identical (verified one flag apart, AE=0), which is exactly why it must never ship silently: it is the A/B arm, not a mode."
#endif
#ifdef FRUA_BLITBENCH
#error "FRUA_BLITBENCH in a RELEASE build: st_init would spend seconds benchmarking memory copies before the menu appears. It is the #48 BLiTTER-vs-CPU measurement harness."
#endif

#endif /* FRUA_RELEASE */
#endif /* FRUA_RELEASE_GUARD_H */
