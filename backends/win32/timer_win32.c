/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>

#include "platform.h"

/* The host backend's timer works under mingw -- clock_gettime and
   nanosleep are both there -- but it works by pulling in libwinpthread,
   which turns a self-contained .exe into one that will not start on a
   machine without that DLL beside it. Windows has had both of these calls
   natively since 1993; using the POSIX shims for them costs a runtime
   dependency and buys nothing. */

u32 plat_ticks_ms(void)
{
    /* GetTickCount64 rather than GetTickCount: the 32-bit one wraps after
       49.7 days of uptime, which is not a hypothetical on a desktop that
       hibernates instead of shutting down. The seam's u32 wraps too, but
       it wraps from a monotonic 64-bit source rather than from a counter
       that has already lost its high bits -- so a caller differencing two
       samples gets the right answer across the boundary. */
    return (u32)GetTickCount64();
}

void plat_sleep_ms(u32 ms)
{
    Sleep((DWORD)ms);
}
