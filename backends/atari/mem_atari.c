/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdlib.h>
#include "platform.h"
#include "mem_atari.h"

/* mintlib's malloc/realloc/free work transparently over GEMDOS on plain
   TOS -- no raw Malloc()/Mfree() trap calls needed. */

static u32 g_alloc_count = 0;
static u32 g_free_count = 0;

void *mem_alloc(u32 size)
{
    void *p;

    if (size == 0) size = 1;
    p = malloc((size_t)size);
    if (p != NULL) g_alloc_count++;
    return p;
}

void *mem_realloc(void *ptr, u32 old_size, u32 new_size)
{
    WP_UNUSED(old_size);

    if (ptr == NULL) return mem_alloc(new_size);
    if (new_size == 0) { mem_free(ptr); return NULL; }
    return realloc(ptr, (size_t)new_size);
}

void mem_free(void *ptr)
{
    if (ptr == NULL) return;
    free(ptr);
    g_free_count++;
}

u32 mem_atari_alloc_count(void)
{
    return g_alloc_count;
}

u32 mem_atari_free_count(void)
{
    return g_free_count;
}

u32 mem_atari_live_count(void)
{
    return g_alloc_count - g_free_count;
}

void mem_atari_reset_counters(void)
{
    g_alloc_count = 0;
    g_free_count = 0;
}
