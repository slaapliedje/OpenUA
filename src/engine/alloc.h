/*
 * FRUA memory allocation.
 *
 * Lifted from the Macintosh build — CODE 3, jump-table entries 387 and 421.
 */

#ifndef ENGINE_ALLOC_H
#define ENGINE_ALLOC_H

/* Allocate `size` bytes; returns NULL on failure. */
void *ua_alloc(unsigned short size);

/*
 * As ua_alloc, but with a full 32-bit byte count (jump-table entry 421) —
 * the same Memory Manager call without ua_alloc's 16-bit narrowing.
 */
void *ua_alloc_long(long size);

#endif /* ENGINE_ALLOC_H */
