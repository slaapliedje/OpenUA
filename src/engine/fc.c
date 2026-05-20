/*
 * FRUA file-cache (FC) subsystem — lifted from CODE 3.
 *
 *   fc_init           jump-table entry 463 (CODE 3 + 0x538, "FCInit")
 *   fc_setup          jump-table entry 464 (CODE 3 + 0x644, "FCSetup")
 *   fc_cleanup        jump-table entry 466 (CODE 3 + 0x632, "FCCleanup")
 *   fc_make_room      L11ca (CODE 3 + 0x11ca)
 *   fc_remove_record  L103c (CODE 3 + 0x103c)
 *
 * The Mac build's standard-library helpers collapse to the C library:
 * L39ae=strlen, L3952=strcpy, L366a/L57f8=memmove, L46b2/L466a=tolower,
 * L39d2=memset, L3b4e=max. See docs/decompilation.md for the subsystem map.
 */

#include <ctype.h>             /* tolower */
#include <string.h>            /* memset, memmove, strcpy, strrchr */

#include "fc.h"
#include "error.h"             /* ua_error  -- the CODE 5 error reporter */
#include "rand.h"              /* ua_rand   -- CODE 5 jump-table entry 1083 */
#include "macmemory.h"         /* NewPtr, DisposePtr, FreeMem  -- compat/ shim */

/* --- FC state: the Mac A5-world globals --- */
unsigned char g_fc_group_table[FC_MAX_GROUPS];   /* A5-10074 */
fc_record_t   g_fc_records[FC_MAX_RECORDS];      /* A5-10026 */
short         g_fc_record_count;                 /* A5-9306  */
char         *g_fc_buffers[FC_MAX_RECORDS + 1];  /* A5-10270 */
char         *g_fc_buf_end;                      /* A5-9304  */
long          g_fc_buf_size;                     /* A5-9300  */

/* FC-internal bookkeeping. */
static unsigned char g_fc_mru[FC_MAX_RECORDS + 1];  /* A5-9354: most-recently-used record indices */
static short         g_fc_record_high;             /* A5-9296: high-water record count           */
static short         g_fc_purge_a;                 /* A5-9294: make-room counter (which != 0)     */
static short         g_fc_purge_b;                 /* A5-9292: make-room counter (which == 0)     */

/* L3e0c: index of the first `byte` in buf[0..count), or count if absent. */
static short fc_index_of(const unsigned char *buf, short count,
                         unsigned char byte)
{
	short i;

	for (i = 0; i < count && buf[i] != byte; i++)
		;
	return i;
}

/*
 * fc_remove_record — CODE 3 + 0x103c ("L103c").
 *
 * Drops record `rec`: frees any group bound to it, decrements every higher
 * record index (the records above `rec` shift down), and removes `rec` from
 * the MRU list, decrementing higher entries there too.
 */
static void fc_remove_record(short rec)
{
	short i;

	for (i = 0; i < FC_MAX_GROUPS; i++) {
		if ((signed char)g_fc_group_table[i] == rec)
			g_fc_group_table[i] = 0xFF;
		else if ((signed char)g_fc_group_table[i] > rec)
			g_fc_group_table[i]--;

		if (i < g_fc_record_count) {
			if (g_fc_mru[i] == rec)
				memmove(&g_fc_mru[i], &g_fc_mru[i + 1],
				        (size_t)(g_fc_record_count - i - 1));
			if ((signed char)g_fc_mru[i] > rec)
				g_fc_mru[i]--;
		}
	}
}

/* L3cfa: copy the path leaf of `src` (the part after the last ':') to `dst`. */
static void fc_leafname(const char *src, char *dst)
{
	const char *colon = strrchr(src, ':');

	strcpy(dst, colon ? colon + 1 : src);
}

/* L3bda: case-insensitive string equality; 1 if equal, 0 if not. */
static int fc_name_eq(const char *a, const char *b)
{
	while (tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
		if (*a == '\0')
			return 1;
		a++;
		b++;
	}
	return 0;
}

/*
 * fc_make_room — CODE 3 + 0x11ca ("L11ca").
 *
 * Evicts one record that no group currently references. ua_rand(3) picks
 * the scan order: by record index, or by MRU position (least-recent last).
 * Returns 1 if a record was evicted, 0 if every record is still in use.
 */
static short fc_make_room(short which)
{
	short i;

	if (which)
		g_fc_purge_a++;
	else
		g_fc_purge_b++;

	if (ua_rand(3)) {
		for (i = g_fc_record_count - 1; i >= 0; i--) {
			if (fc_index_of(g_fc_group_table, FC_MAX_GROUPS,
			                (unsigned char)i) >= FC_MAX_GROUPS) {
				fc_remove_record(i);
				return 1;
			}
		}
		return 0;
	}

	for (i = g_fc_record_count - 1; i >= 0; i--) {
		if (fc_index_of(g_fc_group_table, FC_MAX_GROUPS,
		                g_fc_mru[i]) >= FC_MAX_GROUPS) {
			fc_remove_record(g_fc_mru[i]);
			return 1;
		}
	}
	return 0;
}

/*
 * fc_init — CODE 3 + 0x538.
 *
 * Sizes the data buffer between kb_min*1024 and kb_max*1024, capped at
 * FreeMem()-32768; NewPtr allocates it, retrying at kb_min on failure.
 * THINK C's _NewPtr/_FreeMem trap glue collapses — the calls go straight
 * to the compat/ Memory Manager shim.
 */
void fc_init(short kb_min, short kb_max)
{
	long  want = (long)kb_min * 1024;
	long  cap  = FreeMem() - 32768;
	long  size = (long)kb_max * 1024;
	long  floor_min = (want > cap) ? want : cap;   /* max(want, cap) */
	char *buf;

	g_fc_record_count = 0;
	memset(g_fc_group_table, 0xFF, sizeof g_fc_group_table);
	memset(g_fc_records, 0, sizeof g_fc_records);

	if (size > floor_min)                          /* min(kb_max*1024, .) */
		size = floor_min;

	buf = NewPtr(size);
	if (buf == NULL) {
		size = want;
		buf = NewPtr(size);
	}

	g_fc_buf_end    = buf + size;
	g_fc_buffers[0] = buf;
	if (buf == NULL || want > size)
		ua_error("Insufficient FAR Memory!");
	g_fc_buf_size = size;

	g_fc_record_high = 0;
	g_fc_purge_a = 0;
	g_fc_purge_b = 0;
}

/*
 * fc_setup — CODE 3 + 0x644.
 *
 * Registers file `name` under logical `group`. The bare leaf name is matched
 * (case-insensitively) against the existing records; a hit re-points the
 * group and moves the record to the front of the MRU list (returns 1). A
 * miss appends a new 14-byte record (returns 0). Returns 1 on error too.
 *
 * The original does not guard the group-table index after the "Invalid
 * group" report — the Mac error reporter is modal — so neither does this.
 */
short fc_setup(const char *name, short group)
{
	char  buf[202];
	short i;

	fc_leafname(name, buf);
	buf[13] = '\0';

	if (group < 0 || group >= FC_MAX_GROUPS)
		ua_error("Invalid group (%d)", group);

	if ((g_fc_group_table[group] & 0x80) == 0)
		ua_error("Group %d in use for '%s'", group,
		         g_fc_records[g_fc_group_table[group]].name);

	for (i = 0; i < g_fc_record_count; i++) {
		if (fc_name_eq(g_fc_records[i].name, buf) && buf[0] != '%') {
			short pos = fc_index_of(g_fc_mru, g_fc_record_count,
			                        (unsigned char)i);
			g_fc_group_table[group] = (unsigned char)i;
			memmove(&g_fc_mru[0], &g_fc_mru[1], (size_t)pos);
			g_fc_mru[0] = (unsigned char)i;
			return 1;
		}
	}

	if (g_fc_record_count >= FC_MAX_RECORDS && !fc_make_room(1)) {
		ua_error("FCSetup: too many file groups");
		return 1;
	}

	strcpy(g_fc_records[g_fc_record_count].name, buf);
	g_fc_group_table[group] = (unsigned char)g_fc_record_count;
	memmove(&g_fc_mru[0], &g_fc_mru[1], (size_t)g_fc_record_count);
	g_fc_mru[0] = (unsigned char)g_fc_record_count;
	g_fc_record_count++;
	g_fc_buffers[g_fc_record_count] = g_fc_buffers[g_fc_record_count - 1];
	if (g_fc_record_count > g_fc_record_high)
		g_fc_record_high = g_fc_record_count;
	return 0;
}

/*
 * fc_cleanup — CODE 3 + 0x632.
 *
 * Resets the record count and group table, then disposes the data buffer.
 * The Mac build routes the reset through L0b7a(0); L0b7a's other path
 * ("forget one file by name") is not lifted yet.
 */
void fc_cleanup(void)
{
	g_fc_record_count = 0;
	memset(g_fc_group_table, 0xFF, sizeof g_fc_group_table);
	DisposePtr(g_fc_buffers[0]);
}
