/*
 * FRUA core initialisation — see core.c.
 *
 * Lifted from CODE 6 (core_init / L4cc0) and CODE 5 (the DB arena).
 */

#ifndef ENGINE_CORE_H
#define ENGINE_CORE_H

/*
 * A data-table descriptor: element count, element size in bytes, and the
 * base of a count * elem_size block. In the Mac build this is three adjacent
 * cells of the A5 world — two words and a pointer.
 */
typedef struct {
	short  count;
	short  elem_size;
	void  *base;
} ua_table_t;

/* DB arena — CODE 5 jump-table entries 1002 / 1004, and L531e. */
void  db_init(long size);       /* allocate the DB arena                 */
void *db_base(void);            /* its base address                      */
void *ua_arena_alloc(short n);  /* bump-allocate n bytes from the arena   */

/* main()'s phase-2 core initialisation — L4cc0. */
void  core_init(void);

#endif /* ENGINE_CORE_H */
