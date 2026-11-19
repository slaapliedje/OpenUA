/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MEM_HOST_H
#define WP_MEM_HOST_H

#include "types.h"

/* Host-only introspection into the mem_alloc/mem_free implementation, used
   by tests to prove no-leak acceptance criteria (e.g. T1.1: "create/destroy
   a document with no leaks"). This header is never included from engine/
   or services/ code -- it is not part of the portable platform.h surface. */

u32  mem_host_alloc_count(void);
u32  mem_host_free_count(void);
u32  mem_host_live_count(void);   /* alloc_count - free_count */
void mem_host_reset_counters(void);

/* Out-of-memory on demand: the next n requests (mem_alloc, and
   mem_realloc that is asked to grow or move) succeed and every one after
   them fails, until mem_host_fail_after(-1). 0 fails the very next. */
void mem_host_fail_after(i32 n);

#endif /* WP_MEM_HOST_H */
