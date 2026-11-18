/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MEM_ATARI_H
#define WP_MEM_ATARI_H

#include "types.h"

/* Test-only introspection into the mem_alloc/mem_free implementation,
   mirroring platform/host/mem_host.h's pattern exactly. Added for T4.4:
   GEMDOS's Malloc(-1L) (see wp_atari_memory_budget_selfcheck in
   app_shell.c) turned out NOT to reflect mintlib's own malloc()/free()
   traffic at all -- mintlib claims a heap pool from GEMDOS once at
   startup and manages it internally, so Malloc(-1L) only moves if a
   leak is large enough to force that pool to grow. These counters are
   the real leak signal; Malloc(-1L) still separately proves the process
   stays within real 1MB headroom. Never called from engine/ or
   services/ code. */

u32  mem_atari_alloc_count(void);
u32  mem_atari_free_count(void);
u32  mem_atari_live_count(void);   /* alloc_count - free_count */
void mem_atari_reset_counters(void);

#endif /* WP_MEM_ATARI_H */
