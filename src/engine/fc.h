/*
 * FRUA file-cache (FC) subsystem.
 *
 * Manages FRUA's data files: a large allocated buffer holds file data, and
 * up to FC_MAX_GROUPS logical "groups" map onto FC_MAX_RECORDS de-duplicated
 * file records. Lifted from CODE 3 — see docs/decompilation.md.
 */

#ifndef ENGINE_FC_H
#define ENGINE_FC_H

#define FC_MAX_GROUPS   48
#define FC_MAX_RECORDS  48
#define FC_NAME_LEN     14      /* bytes per file record: 13 chars + NUL */

typedef struct {
	char name[FC_NAME_LEN];
} fc_record_t;

/* FC data model — the Mac build's A5-world globals. */
extern unsigned char g_fc_group_table[FC_MAX_GROUPS];  /* group -> record idx */
extern fc_record_t   g_fc_records[FC_MAX_RECORDS];
extern short         g_fc_record_count;
extern char         *g_fc_buffers[FC_MAX_RECORDS + 1]; /* data-buffer pointers */
extern char         *g_fc_buf_end;
extern long          g_fc_buf_size;

/*
 * Allocate the file-cache buffer and reset the group/record tables. The
 * buffer aims for kb_max KB, but is at least kb_min and within available
 * memory. Lifted from CODE 3 jump-table entry 463 ("FCInit").
 */
void fc_init(short kb_min, short kb_max);

/*
 * Register data file `name` under logical `group`. Re-uses an existing
 * record if the file is already known (returns 1), else appends a new one
 * (returns 0). Lifted from CODE 3 jump-table entry 464 ("FCSetup").
 */
short fc_setup(const char *name, short group);

/*
 * Reset the group/record tables and release the data buffer.
 * Lifted from CODE 3 jump-table entry 466 ("FCCleanup").
 */
void fc_cleanup(void);

#endif /* ENGINE_FC_H */
