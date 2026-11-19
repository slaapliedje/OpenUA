/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef RP_TIMER_HOST_H
#define RP_TIMER_HOST_H

#include "types.h"

/* Host backend only, for tests: every later plat_ticks_ms() reading is
   shifted by `bias` (modulo 2^32). Setting it to (0 - plat_ticks_ms()
   - 100) as read with a bias of 0 puts the clock 100 ms short of the
   wrap, which is where a deadline written as `now >= start + timeout`
   goes wrong. 0 restores the real clock. */
void timer_host_set_bias(u32 bias);

#endif
