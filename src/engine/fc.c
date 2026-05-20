/*
 * FRUA file-cache (FC) subsystem — lifted from CODE 3.
 *
 *   fc_init   jump-table entry 463 (CODE 3 + 0x538, "FCInit")
 *
 * FCSetup/FCCleanup and the rest of the FC API are still to be lifted; see
 * docs/decompilation.md for the subsystem map.
 */

#include <string.h>             /* memset */

#include "fc.h"
#include "error.h"              /* ua_error  -- the CODE 5 error reporter */
#include "macmemory.h"          /* NewPtr, FreeMem  -- compat/ shim       */

/* --- FC state: the Mac A5-world globals --- */
unsigned char g_fc_group_table[FC_MAX_GROUPS];   /* A5-10074 */
fc_record_t   g_fc_records[FC_MAX_RECORDS];      /* A5-10026 */
short         g_fc_record_count;                 /* A5-9306  */
char         *g_fc_buffers[FC_MAX_RECORDS + 1];  /* A5-10270 */
char         *g_fc_buf_end;                      /* A5-9304  */
long          g_fc_buf_size;                     /* A5-9300  */

/* Runtime cursors (A5-9296/-9294/-9292); FC-internal, reset by fc_init.
 * Their individual roles get pinned as more FC routines are lifted. */
static short  g_fc_cursors[3];

/*
 * fc_init — CODE 3 + 0x538.
 *
 * The original clears the record count, memsets the group table to 0xFF and
 * the records to zero (via the L39d2 memset helper), then sizes the data
 * buffer: it aims for kb_max*1024, clamped to at least kb_min*1024 and to
 * (FreeMem() - 32768). NewPtr allocates it, with a retry at kb_min*1024 on
 * failure; too small or NULL raises "Insufficient FAR Memory!".
 *
 * THINK C's _NewPtr and _FreeMem trap glue (JT[1028]/JT[1026]) collapses
 * away — the calls go straight to the compat/ Memory Manager shim.
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

	g_fc_cursors[0] = g_fc_cursors[1] = g_fc_cursors[2] = 0;
}
