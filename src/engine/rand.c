/*
 * FRUA random numbers — lifted from CODE 5, jump-table entry 1083.
 *
 * A linear-congruential generator: seed = seed*0x6D25 + 1. The Mac build
 * reaches the multiply and divides through THINK C's 32-bit arithmetic
 * trap glue (JT[4]/[5]/[7]); those collapse to plain C operators here.
 */

#include "rand.h"

static long g_rand_seed;        /* the LCG state — A5-4902 in the Mac build */

short ua_rand(short n)
{
	long q, r;

	if (n <= 1)
		return 0;
	q = (n + 0x3FFFFFFFL) / n;
	g_rand_seed = g_rand_seed * 0x6D25L + 1;
	r = g_rand_seed & 0x3FFFFFFFL;
	return (short)(r / q);
}
