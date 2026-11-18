/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <proto/dos.h>
#include <dos/dos.h>
#include "platform.h"

/* Real yielding Delay() (dos.library) -- unlike the Atari backend's
   busy-wait (only ever justified by plain TOS having no yield
   primitive), AmigaOS has always had one.

   AmigaDOS ticks are fixed at TICKS_PER_SECOND (dos/dos.h, = 50) by API
   contract -- this is a DOS-level scheduler tick, not tied to the
   video chipset's actual PAL/NTSC vblank rate, so no runtime detection
   is needed. DateStamp() gives whole days + minutes-since-midnight +
   ticks-within-the-minute; plat_ticks_ms tracks a delta from the first
   call (same relative-clock approach as the Atari backend's clock()
   wrapper) rather than trying to expose an absolute epoch. */

static wp_bool g_have_base = WP_FALSE;
static u32 g_base_ticks = 0;

static u32 total_ticks(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (((u32)ds.ds_Days * 24UL * 60UL) + (u32)ds.ds_Minute)
           * 60UL * (u32)TICKS_PER_SECOND + (u32)ds.ds_Tick;
}

u32 plat_ticks_ms(void)
{
    u32 t = total_ticks();

    if (!g_have_base) {
        g_base_ticks = t;
        g_have_base = WP_TRUE;
    }
    return (t - g_base_ticks) * 1000UL / (u32)TICKS_PER_SECOND;
}

void plat_sleep_ms(u32 ms)
{
    Delay((LONG)(ms * (u32)TICKS_PER_SECOND / 1000UL));
}
