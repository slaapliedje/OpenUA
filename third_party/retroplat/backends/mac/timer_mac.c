/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <OSUtils.h>
#include "platform.h"

/* TickCount() (OSUtils.h, trap 0xA975) returns ticks since boot at a
   fixed ~60.15/sec rate (Inside Macintosh: Operating System Utilities) --
   a hardware-independent value the Toolbox itself guarantees, not tied
   to any particular Mac model's real vertical-retrace rate, so no
   runtime detection is needed (same reasoning timer_amiga.c gives for
   trusting AmigaDOS's fixed TICKS_PER_SECOND). plat_ticks_ms tracks a
   delta from the first call, same relative-clock approach as
   timer_atari.c's clock() wrapper and timer_amiga.c's DateStamp()
   wrapper. Delay() (also OSUtils.h) yields to other processes while
   waiting, unlike Atari's busy-wait (which was only ever justified by
   plain TOS having no yield primitive). */

#define MAC_TICKS_PER_SEC 60UL

static wp_bool g_have_base = WP_FALSE;
static u32 g_base_ticks = 0;

u32 plat_ticks_ms(void)
{
    u32 t = (u32)TickCount();

    if (!g_have_base) {
        g_base_ticks = t;
        g_have_base = WP_TRUE;
    }
    return (t - g_base_ticks) * 1000UL / MAC_TICKS_PER_SEC;
}

void plat_sleep_ms(u32 ms)
{
    long final_ticks;

    Delay((long)(ms * MAC_TICKS_PER_SEC / 1000UL), &final_ticks);
}
