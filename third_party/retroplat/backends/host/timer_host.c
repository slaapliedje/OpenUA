/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <time.h>
#include "platform.h"

u32 plat_ticks_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)((u32)ts.tv_sec * 1000u + (u32)(ts.tv_nsec / 1000000L));
}

void plat_sleep_ms(u32 ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000L);
    nanosleep(&ts, NULL);
}
