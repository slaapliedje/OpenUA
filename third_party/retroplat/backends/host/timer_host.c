/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <time.h>
#include "platform.h"
#include "timer_host.h"

static u32 g_bias = 0;

/* Test hook (timer_host.h): shifts every later reading by `bias`, so a
   test can stand the clock a few milliseconds short of the u32 wrap. */
void timer_host_set_bias(u32 bias)
{
    g_bias = bias;
}

u32 plat_ticks_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)((u32)ts.tv_sec * 1000u + (u32)(ts.tv_nsec / 1000000L)) + g_bias;
}

void plat_sleep_ms(u32 ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000L);
    nanosleep(&ts, NULL);
}
