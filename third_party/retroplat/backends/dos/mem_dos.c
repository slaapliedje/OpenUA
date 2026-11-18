/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdlib.h>
#include "platform.h"
#include "mem_dos.h"

/* Open Watcom's own DOS libc malloc/realloc/free work directly against
   the small memory model's near heap (DGROUP) -- no raw INT 21h
   allocation calls needed, the same "just use the cross-compiler's own
   libc" shape every prior backend uses. */

/* A 16-bit model's size_t is 16 bits -- one block is at most 64 KB, in
   the large model as in the small -- so a bigger request would be cut to
   its low 16 bits by the cast below and come back as a small block that
   the caller then overruns. Refused instead, as a caller expects of a
   request the heap cannot meet. */
#define DOS_ALLOC_MAX 65520uL

static u32 g_alloc_count = 0;
static u32 g_free_count = 0;

void *mem_alloc(u32 size)
{
    void *p;

    if (size == 0) size = 1;
    if (size > DOS_ALLOC_MAX) return NULL;
    p = malloc((size_t)size);
    if (p != NULL) g_alloc_count++;
    return p;
}

void *mem_realloc(void *ptr, u32 old_size, u32 new_size)
{
    WP_UNUSED(old_size);

    if (ptr == NULL) return mem_alloc(new_size);
    if (new_size == 0) { mem_free(ptr); return NULL; }
    if (new_size > DOS_ALLOC_MAX) return NULL;
    return realloc(ptr, (size_t)new_size);
}

void mem_free(void *ptr)
{
    if (ptr == NULL) return;
    free(ptr);
    g_free_count++;
}

u32 mem_dos_alloc_count(void)
{
    return g_alloc_count;
}

u32 mem_dos_free_count(void)
{
    return g_free_count;
}

u32 mem_dos_live_count(void)
{
    return g_alloc_count - g_free_count;
}

void mem_dos_reset_counters(void)
{
    g_alloc_count = 0;
    g_free_count = 0;
}
