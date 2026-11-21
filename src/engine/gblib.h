/*
 * gblib.h — the Gold Box games' GLIB/HLIB 'DATA' library, read through a
 * callback (ADR-0026). Plain C99 with no engine, shim or platform headers.
 *
 * A library is a 16-byte header ("GLIB" big-endian or "HLIB" little-endian,
 * u32 size, u16 member count, u16, "DATA"), an offset per member and one for
 * the end, then the members. Member 0 is the id table: u16 count, then
 * count x {u16 id, u16 member index}. tools/gbgeo.py (read_library,
 * read_id_table) is the reference.
 */
#ifndef ENGINE_GBLIB_H
#define ENGINE_GBLIB_H

#include "rules_gb.h"           /* gbr_read_fn */

#define GBL_MEMBERS 512

#define GBL_ENOTLIB -1          /* not a GLIB/HLIB library                  */
#define GBL_ENONE   -2          /* no such member                           */
#define GBL_EREAD   -3

typedef struct gbl_lib {
	int           big;                      /* GLIB: big-endian             */
	unsigned      members;                  /* offsets known: members + 1   */
	unsigned      ids;                      /* entries in the id table      */
	unsigned long off[GBL_MEMBERS + 1];
	unsigned short id[GBL_MEMBERS], index[GBL_MEMBERS];
} gbl_lib;

/* Read the header, offsets and id table. GBL_OK (0) or a GBL_E code. */
int gbl_open(gbr_read_fn rd, void *ctx, gbl_lib *lib);

/* Entry `i` of the id table (file order): its id, and where its member is.
 * The member found by id is the LAST entry with that id, as a Python dict
 * built from the table has it. Each returns GBL_OK or GBL_ENONE. */
int gbl_entry(const gbl_lib *lib, unsigned i, unsigned *id, long *at, long *len);
int gbl_find(const gbl_lib *lib, unsigned id, long *at, long *len);

#endif
