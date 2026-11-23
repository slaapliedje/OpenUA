/*
 * gbcells.h — the Dark Queen's character cells (script addresses
 * 0x401..0x800), read and written on the selected character's record.
 *
 * DQK.EXE serves that range from the selected character (0x406cd read,
 * 0x41e1e write); the reference, with the derivation, is tools/gbcells.py,
 * and tests/test_gbcells.py holds this file to it and it to DQK.EXE.
 *
 * `rec` is the engine's record (FRUA's layout, in memory, after L0ce0).
 * Plain C99, no engine, shim or platform headers.
 */
#ifndef FRUA_GBCELLS_H
#define FRUA_GBCELLS_H

#define GBC_LO 0x401
#define GBC_HI 0x800

/* Is `addr` a character cell? */
#define GBC_IS_CELL(addr) ((addr) >= GBC_LO && (addr) <= GBC_HI)

/* What a script reads at `cell` (an unmapped cell reads 0). */
unsigned short gbc_read(const unsigned char *rec, unsigned cell);

/* A script's write of `v` to `cell` (an unmapped one changes nothing). */
void gbc_write(unsigned char *rec, unsigned cell, unsigned short v);

#endif
