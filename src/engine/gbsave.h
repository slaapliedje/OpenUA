/*
 * gbsave.h — a Dark Queen of Krynn saved game (SAVGAMx.QSV) read as the game
 * writes it (ADR-0026 step (f)). Plain C99 with no engine, shim or platform
 * headers, so it builds on the host for tests/test_gbsave.py; the caller
 * reads the file.
 *
 * tools/gbsave.py is the reference and its docstring has the layout: script
 * memory one byte a cell, the party's square and facing, then the party as
 * .QCH records back to back (src/engine/qch.c reads each).
 */
#ifndef ENGINE_GBSAVE_H
#define ENGINE_GBSAVE_H

#define GBV_SIZE      7965      /* every save the game writes              */
#define GBV_CELLS     1024      /* script cells 0x001..0x400               */
#define GBV_MEMBERS   8

#define GBV_CELL_FACING 0x11    /* 0=N 1=E 2=S 3=W                         */
#define GBV_CELL_MAP    0x13    /* the map loaded                          */
#define GBV_CELL_SCRIPT 0x16    /* the script running                      */
#define GBV_CELL_PLACE  0x0d    /* the selected member's place, from 0     */
#define GBV_CELL_SEARCH 0x19    /* 1 while searching (Search on, or Look)  */
#define GBV_CELL_PARTY  0x20    /* members in the party, companions too    */
/* The clock is script memory: cells 5..11 are rounds, minutes, tens of
 * minutes, hours, days, months and years (FRUA's game record h[5..11],
 * the same seven fields and radices 10/10/6/24/30/12/100). */
#define GBV_CELL_CLOCK  0x05
#define GBV_CLOCK_N     7

/* VAULT<c>.DAT: FRUA's vault file (12 bytes of money, 0xffff and a count,
 * the items) with 17-byte item records, always 50 of them. */
#define GBV_VAULT_SIZE  866

#define GBV_ESHORT   -1         /* shorter than the header                 */
#define GBV_ECOUNT   -2         /* more party records than a party holds   */
#define GBV_ERECORD  -3         /* a record runs past the end              */

typedef struct gbv_save {
	const unsigned char *cells;             /* cell n is cells[n - 1]       */
	unsigned char x, y;                     /* x east, y south              */
	unsigned char facing;                   /* 0=N 1=E 2=S 3=W              */
	unsigned char desc;                     /* the square's descriptor      */
	unsigned char map, script;
	unsigned      members;
	long          at[GBV_MEMBERS];          /* each record, a .QCH          */
	long          len[GBV_MEMBERS];
} gbv_save;

/* Read the save `q` (`len` bytes; it must stay in memory while `s` is used).
 * 0, or a GBV_E code. */
int gbv_read(const unsigned char *q, long len, gbv_save *s);

/* The length of the record at `at`, walked as the game links it, or a GBV_E
 * code. */
long gbv_record_length(const unsigned char *q, long len, long at);

#endif
