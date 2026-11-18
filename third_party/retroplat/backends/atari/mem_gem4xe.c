/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* mem_gem4xe.c -- mem_alloc/mem_free/mem_realloc on gem4xe.
 *
 * The ST's half of this seam (mem_atari.c) is mintlib's malloc, which
 * manages a pool it claims from GEMDOS once and hands back real free
 * space on free().  gem4xe's GEMDOS cannot be used that way, and says so
 * (gem4xe/src/sys/gemdos.h): "Malloc is the far heap: the bump
 * allocator, wound back when the application exits, so Mfree is a no-op
 * and a leak lasts one run."
 *
 * That is the right contract for the programs gem4xe was written for --
 * a desk accessory, a calculator, a shell that runs one program and
 * reclaims everything it touched.  It is the wrong one for an editor.
 * RetroWP frees on every run split, every undo push, every relayout; over
 * a session the engine allocates many times the size of the document it
 * is holding.  Against a no-op free, a 14.9 MB heap is not a large heap,
 * it is a long fuse.
 *
 * So the seam is met where it is defined rather than where it is
 * convenient: Malloc supplies ARENAS and a first-fit allocator runs
 * inside them.  Malloc is called a handful of times per run and Mfree
 * never, which is exactly the usage gem4xe's heap is built for.
 *
 * Arenas are chained, so there is no fixed ceiling: when no arena can
 * satisfy a request another is taken.  They are never given back -- that
 * IS the bump allocator's contract, and honouring it costs one Malloc per
 * 32 KB rather than a leak per free.
 *
 * WHY THE SENTINEL.  All arenas live on ONE address-ordered block list,
 * and coalescing merges a free block with the free block after it.  Two
 * arenas are not adjacent in memory, so merging across the join would
 * hand out a block spanning a gap.  Each arena therefore ends in a
 * zero-length block marked USED, which no merge will pass: the invariant
 * is kept by construction rather than by a comparison that has to be
 * right every time.
 *
 * far_alloc will not hand out a block that crosses a bank
 * (gem4xe/src/sys/farmem.c), so an arena is asked for in a size that
 * comfortably fits one. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include <string.h>        /* memcpy, for mem_realloc */
#include "platform.h"
#include "mem_atari.h"

#define ALIGN        4u
#define ARENA_BYTES  32768uL    /* well inside one 64 KB bank */

typedef struct blk {
    struct blk *next;           /* address order, 0 at the end of the chain */
    u32         size;           /* payload bytes, a multiple of ALIGN */
    u16         used;
    u16         pad;            /* keep the header a multiple of ALIGN */
} blk;

static blk *g_head;             /* first block of the first arena */
static blk *g_tail;             /* the last arena's sentinel */

static u32 g_alloc_count = 0;
static u32 g_free_count  = 0;

#define HDR ((u32)sizeof(blk))

static u32 round_up(u32 n)
{
    return (n + (ALIGN - 1u)) & ~(u32)(ALIGN - 1u);
}

/* One arena: a free block big enough for `need`, then the USED sentinel
   that stops a merge at the arena's top edge. */
static int arena_new(u32 need)
{
    u32   want = ARENA_BYTES;
    u32   min  = round_up(need) + HDR + HDR;
    LONG  got;
    blk  *first, *end;

    if (min > want) want = round_up(min);

    /* Two guards a wrong size taught (RetroWP b345046, 2026-11-05): a
       request past the 65816's 16 MB can only be a garbage size, and must
       not reach Malloc, where a negative LONG once meant "how much is
       free" and answered with a block; and an answer below $01:0000 is
       not an arena -- gem4xe never hands out bank $00, where a heap header
       lands on the PIA and banks the stack away. Either is a plain
       failure: mem_alloc returns NULL and the caller says WP_NOMEM. */
    if (need > 0x00FFFFFFuL || want > 0x00FFFFFFuL) return 0;
    got = Malloc((LONG)want);
    if (got < 0x10000L) return 0;

    first = (blk *)(void *)(u32)got;
    first->size = want - HDR - HDR;
    first->used = 0;
    first->pad  = 0;

    end = (blk *)(void *)((u8 *)first + HDR + first->size);
    end->next = (blk *)0;
    end->size = 0;
    end->used = 1;                      /* the sentinel; never merged past */
    end->pad  = 0;

    first->next = end;

    if (g_head == (blk *)0) g_head = first;
    else                    g_tail->next = first;
    g_tail = end;
    return 1;
}

/* Merge every free block with the free block after it.  Called only when
   a first-fit walk has already failed, so its cost is paid once per
   growth rather than once per free. */
static void coalesce_all(void)
{
    blk *b = g_head;

    while (b != (blk *)0 && b->next != (blk *)0) {
        if (!b->used && !b->next->used) {
            b->size += HDR + b->next->size;
            b->next  = b->next->next;
            continue;                   /* try to swallow the next one too */
        }
        b = b->next;
    }
}

static blk *first_fit(u32 want)
{
    blk *b;

    for (b = g_head; b != (blk *)0; b = b->next) {
        if (!b->used && b->size >= want) return b;
    }
    return (blk *)0;
}

/* Hand back the tail of a block that is bigger than the request needs --
   but only when what is left over could itself carry a payload.  A split
   that leaves less than a header plus ALIGN would produce a block nothing
   can ever be put in. */
static void split(blk *b, u32 want)
{
    blk *rest;

    if (b->size < want + HDR + ALIGN) return;

    rest = (blk *)(void *)((u8 *)b + HDR + want);
    rest->size = b->size - want - HDR;
    rest->used = 0;
    rest->pad  = 0;
    rest->next = b->next;

    b->size = want;
    b->next = rest;
}

void *mem_alloc(u32 size)
{
    u32  want;
    blk *b;

    if (size == 0) size = 1;
    want = round_up(size);

    b = first_fit(want);
    if (b == (blk *)0) {
        coalesce_all();
        b = first_fit(want);
    }
    if (b == (blk *)0) {
        if (!arena_new(want)) return (void *)0;
        b = first_fit(want);
        if (b == (blk *)0) return (void *)0;
    }

    split(b, want);
    b->used = 1;
    g_alloc_count++;
    return (void *)((u8 *)b + HDR);
}

void mem_free(void *ptr)
{
    blk *b;

    if (ptr == (void *)0) return;

    b = (blk *)(void *)((u8 *)ptr - HDR);
    b->used = 0;

    /* Forward merge only: cheap, and it is what keeps a run of frees from
       leaving a run of unusable fragments.  Backward merging is what
       coalesce_all does, on the path that actually needs it. */
    while (b->next != (blk *)0 && !b->next->used) {
        b->size += HDR + b->next->size;
        b->next  = b->next->next;
    }
    g_free_count++;
}

void *mem_realloc(void *ptr, u32 old_size, u32 new_size)
{
    blk  *b;
    void *fresh;
    u32   copy;

    if (ptr == (void *)0) return mem_alloc(new_size);
    if (new_size == 0) { mem_free(ptr); return (void *)0; }

    b = (blk *)(void *)((u8 *)ptr - HDR);

    /* Already big enough: keep the address.  Growing in place by taking
       the next block is deliberately NOT done -- it would have to undo a
       split and re-link, and a copy of a few hundred bytes on this
       machine is cheaper than getting that wrong. */
    if (b->size >= round_up(new_size)) return ptr;

    fresh = mem_alloc(new_size);
    if (fresh == (void *)0) return (void *)0;

    copy = (old_size < new_size) ? old_size : new_size;
    if (copy > b->size) copy = b->size;
    if (copy > 0) memcpy(fresh, ptr, (size_t)copy);

    mem_free(ptr);
    return fresh;
}

u32  mem_atari_alloc_count(void) { return g_alloc_count; }
u32  mem_atari_free_count(void)  { return g_free_count; }
u32  mem_atari_live_count(void)  { return g_alloc_count - g_free_count; }

void mem_atari_reset_counters(void)
{
    g_alloc_count = 0;
    g_free_count  = 0;
}

#else

typedef int rp_mem_gem4xe_not_this_target;

#endif
