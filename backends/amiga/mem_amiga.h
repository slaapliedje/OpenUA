/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MEM_AMIGA_H
#define WP_MEM_AMIGA_H

#include "types.h"

/* Test-only introspection into the mem_alloc/mem_free implementation,
   mirroring platform/atari/mem_atari.h's pattern exactly. Never called
   from engine/ or services/ code. */

u32  mem_amiga_alloc_count(void);
u32  mem_amiga_free_count(void);
u32  mem_amiga_live_count(void);   /* alloc_count - free_count */
void mem_amiga_reset_counters(void);

#endif /* WP_MEM_AMIGA_H */
