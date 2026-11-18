/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* timer_gem4xe.c -- plat_ticks_ms / plat_sleep_ms on gem4xe.
 *
 * The ST's half of this seam (timer_atari.c) reads mintlib's clock(), and
 * gem4xe's kit supplies a clock() too -- over Tgettimeofday (GEMDOS 0x155,
 * MiNT's call, served from the ~4 kHz timer gem4xe already runs for the
 * pointer), so the ST's file LINKS here and answers real milliseconds.
 * This file exists because linking and being right are not the same thing:
 * two facts about that clock differ from the ST's, and one of them turns a
 * line that is harmless there into a bug here.
 *
 * THE RATE.  CLOCKS_PER_SEC is 1000 on this toolchain, not mintlib's 200,
 * so a tick IS a millisecond.  The ST's line scales by multiplying by 1000
 * first and dividing by CLOCKS_PER_SEC after -- at this rate the identity,
 * computed the one way that cannot survive it: (u32)c * 1000 overflows
 * thirty-two bits at c = 4,294,967, so seventy-one minutes into a session
 * plat_ticks_ms would stop being monotonic and start answering nonsense.
 * Dividing before multiplying, and carrying the remainder separately,
 * costs nothing and holds until the millisecond count itself runs out of
 * u32 -- forty-nine days.  It is what the kit's own clock() does, for this
 * same reason.
 *
 * THE ORIGIN.  gem4xe's clock() counts from the program's FIRST CALL to
 * it; mintlib's counts _hz_200 since the machine came up.  Everything in
 * this tree takes differences, so it does not matter -- but a caller that
 * expected a large value at startup would be wrong here, and the seam does
 * not promise an origin in either direction.
 *
 * SLEEPING YIELDS.  The ST's version spins on the clock, which was the
 * right answer on plain TOS (no Fsleep to assume) and is the wrong one
 * here: a spin that never calls the AES starves the desk accessories and
 * the AES's own timer events -- everything else on the machine, that is,
 * for the whole span.  evnt_timer is the same wait and gives the AES the
 * chance to run somebody else, which is what the kit's README asks for.
 * Its two arguments are the millisecond count already split into words,
 * the split being the binding's, not the AES's. */

#include <gem.h>

/* Guarded on gem4xe's own include guard, and the else-branch keeps the
 * translation unit legal (C89 3.7), for the reasons file_gem4xe.c gives at
 * length: this directory is compiled whole by both GEMs and by retroplat's
 * seam gate, and no build can be asked to know which half is its own. */
#ifdef GEM4XE_APP_GEM_H

#include <time.h>
#include "platform.h"

u32 plat_ticks_ms(void)
{
    u32 c = (u32)clock();

    /* Whole seconds, then what is left of one -- so nothing is multiplied
       by 1000 until it is small enough to survive it.  At CLOCKS_PER_SEC
       1000 this is the identity on c, which is the point: it stays the
       identity, rather than being one through an overflow. */
    return (c / (u32)CLOCKS_PER_SEC) * 1000UL
         + ((c % (u32)CLOCKS_PER_SEC) * 1000UL) / (u32)CLOCKS_PER_SEC;
}

void plat_sleep_ms(u32 ms)
{
    if (ms == 0) return;

    (void)evnt_timer((UWORD)(ms & 0xFFFFUL), (UWORD)((ms >> 16) & 0xFFFFUL));
}

#else

typedef int rp_timer_gem4xe_not_this_target;

#endif
