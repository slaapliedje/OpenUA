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
#if defined(FRUA_ENTRY_LEVEL) || defined(FRUA_ENTRY_ROW) \
 || defined(FRUA_ENTRY_COL) || defined(FRUA_ENTRY_FACING)
#error "FRUA_ENTRY_* in a RELEASE build: the party would be teleported to a hard-coded cell on every play entry, ignoring the design's start area."
#endif

#endif /* FRUA_RELEASE */
#endif /* FRUA_RELEASE_GUARD_H */
