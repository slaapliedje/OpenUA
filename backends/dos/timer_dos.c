/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <time.h>
#include "platform.h"

/* Open Watcom's own DOS libc clock() is driven by the BIOS system tick
   (INT 1Ah, ~18.2Hz) -- no raw interrupt calls needed, the same "just use
   the cross-compiler's own libc" shape platform/atari/timer_atari.c
   already uses for mintlib's own clock(). Integer math only, same
   reasoning as that file's own comment.

   The tick is 18.2Hz but CLOCKS_PER_SEC here is 1000: Watcom scales it, so
   clock() answers milliseconds in ~55ms steps. Granularity, not rate --
   and it means this file had the same overflow gem4xe's 1000Hz clock
   exposed (timer_gem4xe.c): c * 1000 before the divide overflows 32 bits
   at c = 4,294,967, which at this rate is seventy-one minutes of run time,
   after which plat_ticks_ms stops being monotonic. Dividing first and
   carrying the remainder holds to the forty-nine days a u32 millisecond
   count can express. */

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
        /* Busy-wait: no DOS-native yield is assumed available on plain
           real-mode DOS. Sleep durations in this app are short and rare
           (no offload device is wired up yet -- T14.4 -- so nothing
           currently calls this with a large ms), matching timer_atari.c's
           own identical reasoning. */
    }
}
