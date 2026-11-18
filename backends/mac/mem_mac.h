/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_MEM_MAC_H
#define WP_MEM_MAC_H

#include "types.h"

/* Test-only introspection into the mem_alloc/mem_free implementation,
   mirroring platform/atari/mem_atari.h and platform/amiga/mem_amiga.h
   exactly. Never called from engine/ or services/ code. */

u32  mem_mac_alloc_count(void);
u32  mem_mac_free_count(void);
u32  mem_mac_live_count(void);   /* alloc_count - free_count */
void mem_mac_reset_counters(void);

#endif /* WP_MEM_MAC_H */
