/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MEM_DOS_H
#define WP_MEM_DOS_H

#include "types.h"

/* Test-only introspection into the mem_alloc/mem_free implementation,
   mirroring platform/atari/mem_atari.h's pattern exactly. Never called
   from engine/ or services/ code. */

u32  mem_dos_alloc_count(void);
u32  mem_dos_free_count(void);
u32  mem_dos_live_count(void);   /* alloc_count - free_count */
void mem_dos_reset_counters(void);

#endif /* WP_MEM_DOS_H */
