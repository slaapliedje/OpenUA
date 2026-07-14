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

#endif /* FRUA_RELEASE */
#endif /* FRUA_RELEASE_GUARD_H */
