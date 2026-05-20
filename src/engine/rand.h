/*
 * FRUA random numbers.
 *
 * Lifted from the Macintosh build — CODE 5, jump-table entry 1083.
 */

#ifndef ENGINE_RAND_H
#define ENGINE_RAND_H

/* A uniform random integer in [0, n); returns 0 when n <= 1. */
short ua_rand(short n);

#endif /* ENGINE_RAND_H */
