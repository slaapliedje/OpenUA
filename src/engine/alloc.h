/*
 * FRUA memory allocation.
 *
 * Lifted from the Macintosh build — CODE 3, jump-table entry 387.
 */

#ifndef ENGINE_ALLOC_H
#define ENGINE_ALLOC_H

/* Allocate `size` bytes; returns NULL on failure. */
void *ua_alloc(unsigned short size);

#endif /* ENGINE_ALLOC_H */
