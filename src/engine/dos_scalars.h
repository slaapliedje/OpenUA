/*
 * Load the shared game-rule tables from the user's DOS CKIT.EXE (ADR-0017).
 * See dos_scalars.c for why this exists and how it verifies.
 *
 * Returns the number of runs applied, 0 when there is no CKIT.EXE (a Mac-only
 * install — expected and harmless), or -1 when the file is present but does
 * not match the shipped map, in which case NOTHING is applied.
 */
#ifndef ENGINE_DOS_SCALARS_H
#define ENGINE_DOS_SCALARS_H

int dos_scalars_load(void);

#endif /* ENGINE_DOS_SCALARS_H */
