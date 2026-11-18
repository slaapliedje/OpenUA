/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <time.h>
#include "platform.h"

/* mintlib's clock() reads the ST's 200Hz system tick. Integer math only
   (no floats) -- CLOCKS_PER_SEC is a small integer constant here, not
   worth pulling in softfloat for.

   Scaled by dividing first and carrying the remainder, rather than the
   obvious c * 1000 / CLOCKS_PER_SEC: that multiply overflows 32 bits at
   c = 4,294,967 ticks, which at 200 Hz is about six hours of uptime --
   after which plat_ticks_ms stops being monotonic and starts answering
   nonsense, in a document the machine's owner has had open all day. This
   form holds until the millisecond count itself runs out of u32, at
   forty-nine days. gem4xe found it at CLOCKS_PER_SEC 1000, where the
   same line fails after seventy-one minutes (timer_gem4xe.c). */

u32 plat_ticks_ms(void)
{
    u32 c = (u32)clock();

    return (c / (u32)CLOCKS_PER_SEC) * 1000UL
         + ((c % (u32)CLOCKS_PER_SEC) * 1000UL) / (u32)CLOCKS_PER_SEC;
}

void plat_sleep_ms(u32 ms)
{
    clock_t start = clock();
    clock_t target_ticks = (clock_t)((ms / 1000UL) * (u32)CLOCKS_PER_SEC
                                     + ((ms % 1000UL) * (u32)CLOCKS_PER_SEC)
                                       / 1000UL);

    while ((clock() - start) < target_ticks) {
        /* Busy-wait: no MiNT/FreeMiNT Fsleep()-style yield is assumed
           available on plain TOS. Sleep durations in this app are short
           and rare (no offload device is wired up yet -- T6.3 -- so
           nothing currently calls this with a large ms).

           Correct here and NOT on gem4xe, where the AES is cooperative
           and a spin that never calls it starves the accessories for the
           whole span -- hence timer_gem4xe.c rather than an #ifdef. */
    }
}
