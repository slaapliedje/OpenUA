/*
 * planar_tile_cache.h — keep converted wall tiles (planar_tile.h) so a repeat
 * blit skips the conversion entirely.
 *
 * SIZED FROM MEASUREMENT (#144), not guessed. Over a 137-key walk the renderer
 * issues 15-28 tile blits per frame but touches only 48-64 DISTINCT tiles in the
 * whole run — the count PLATEAUS — for 28-34 KB in planes+mask form. Reuse WITHIN
 * one frame is only ~1.3-1.6x, so a per-frame cache would be worthless; the win is
 * entirely in persistence across frames.
 *
 * KEY = (slot, idx). Not (slot, idx, band), even though tiles straddle band
 * boundaries constantly (3,544 straddling blits, up to five bands each): the
 * per-band remaps were measured IDENTICAL for the values a tile writes, every
 * time, because quantize.h reserves exact slots for kept colours. `slot` is in the
 * key because the three wall groups rebase into different CLUT bands
 * (g_cw_base[] = 32/64/96), so the same idx is different pixels per group.
 *
 * ★ EPOCH, AND WHY IT IS NOT OPTIONAL. That agreement is SPATIAL — bands agree
 * with each other at one instant. It says nothing about the remap changing over
 * TIME, and a re-band rebuilds every band palette from new content (~21 of them in
 * a driven walk). A tile converted before a re-band may therefore be stale even
 * though every band agreed when it was built. Rather than assume stability the
 * caller passes a remap generation and entries carry the one they were built
 * against; a mismatch is a miss, so the cache is self-correcting whether the remap
 * turns out to be stable or not. If it IS stable in practice the entries simply
 * never rebuild, and pt_cache_stats' `rebuilt` counter says which world we are in.
 *
 * ★ A NEW WALL SET IS NOT A NEW EPOCH. Level change / group rebind (l6eea) makes
 * the same idx mean different ART, which no remap generation can express — the
 * caller must pt_cache_flush() on the binder change.
 */
#ifndef PLATFORM_PLANAR_TILE_CACHE_H
#define PLATFORM_PLANAR_TILE_CACHE_H

#include <stddef.h>              /* NULL */
#include "planar_tile.h"

/* Indices one tile can bake in before the epoch falls back to the whole row.
 * A wall piece draws from its set's CLUT band (CW_BAND = 37 entries) plus the
 * few shared mortar/shadow colours, so 48 covers real art with room to spare. */
#define PT_MAX_USED 48

typedef struct {
	short          slot;              /* wall group; -1 = entry unused     */
	short          idx;               /* piece index within the set        */
	short          w, h;
	unsigned long  epoch;             /* remap fingerprint it was built for */
	long           off;               /* words into the pool               */
	/* ★ WHICH REMAP ENTRIES THIS TILE ACTUALLY DEPENDS ON. Fingerprinting the
	 * whole 256-entry band row invalidates a tile when an index it never draws
	 * moves — measured as a large share of 455 rebuilds over a 125-key walk,
	 * where only 67 were genuine first sightings. Narrowing the fingerprint to
	 * the tile's own indices is both cheaper (nused iterations, not 256) and
	 * strictly more precise. nused == 0 means "more than PT_MAX_USED, fall back
	 * to the whole row" — coarser, never wrong. */
	unsigned char  nused;
	unsigned char  uidx[PT_MAX_USED];
} pt_entry_t;

/* Fingerprint the remap entries a tile depends on. `used`/`nused` come from the
 * entry; nused == 0 means the whole row. */
static unsigned long pt_epoch(const unsigned char *rm_row,
                              const unsigned char *used, short nused)
{
	unsigned long e = 0;
	short i;

	if (nused <= 0) {
		for (i = 0; i < 256; i++)
			e = e * 31u + rm_row[i];
		return e;
	}
	for (i = 0; i < nused; i++)
		e = e * 31u + rm_row[used[i]];
	return e;
}

typedef struct {
	unsigned short *pool;
	long            pool_words, pool_used;
	pt_entry_t     *ent;
	short           n_ent;
	/* stats — `full` and `rebuilt` are the two that decide whether the sizing
	 * above still holds on a drive nobody has tried yet. */
	unsigned long   hit, miss, rebuilt, full;
} pt_cache_t;

static void pt_cache_init(pt_cache_t *c, unsigned short *pool, long pool_words,
                          pt_entry_t *ent, short n_ent)
{
	short i;

	c->pool = pool; c->pool_words = pool_words; c->pool_used = 0;
	c->ent = ent;   c->n_ent = n_ent;
	c->hit = c->miss = c->rebuilt = c->full = 0;
	for (i = 0; i < n_ent; i++)
		ent[i].slot = -1;
}

/* Drop everything — for a wall-set change, where idx means different art. */
static void pt_cache_flush(pt_cache_t *c)
{
	short i;

	c->pool_used = 0;
	for (i = 0; i < c->n_ent; i++)
		c->ent[i].slot = -1;
}

/*
 * Look up (slot, idx). Returns the converted tile when it is present AND was
 * built against `epoch`, else NULL — the caller then calls pt_cache_reserve.
 */
static unsigned short *pt_cache_get(pt_cache_t *c, short slot, short idx,
                                    const unsigned char *rm_row)
{
	short i;

	for (i = 0; i < c->n_ent; i++)
		if (c->ent[i].slot == slot && c->ent[i].idx == idx) {
			/* fingerprint over THIS entry's indices, so a remap entry the
			 * tile never draws cannot invalidate it */
			if (c->ent[i].epoch != pt_epoch(rm_row, c->ent[i].uidx,
			                                c->ent[i].nused))
				return NULL;      /* stale: reserve rebuilds it */
			c->hit++;
			return c->pool + c->ent[i].off;
		}
	return NULL;
}

/*
 * Reserve storage for (slot, idx) at `epoch` and return a writable tile for the
 * caller to planar_tile_build() into. Reuses the existing slab when the entry is
 * merely stale (same geometry), so a re-band does not leak the pool. NULL when
 * the pool or the table is full — the caller keeps its old per-pixel path, which
 * is why running out is a slowdown and never a wrong picture.
 */
static void pt_set_used(pt_entry_t *e, const unsigned char *used, short nused)
{
	short i;

	if (nused < 0 || nused > PT_MAX_USED)
		nused = 0;                          /* fall back to the whole row */
	e->nused = (unsigned char)nused;
	for (i = 0; i < nused; i++)
		e->uidx[i] = used[i];
}

static unsigned short *pt_cache_reserve(pt_cache_t *c, short slot, short idx,
                                        short w, short h,
                                        const unsigned char *rm_row,
                                        const unsigned char *used, short nused)
{
	long need = PT_WORDS_FOR(w, h);
	short i, free_i = -1;

	for (i = 0; i < c->n_ent; i++) {
		if (c->ent[i].slot == slot && c->ent[i].idx == idx) {
			if (c->ent[i].w == w && c->ent[i].h == h) {
				pt_set_used(&c->ent[i], used, nused);
				c->ent[i].epoch = pt_epoch(rm_row, c->ent[i].uidx,
				                           c->ent[i].nused);
				c->rebuilt++;              /* rebuild in place */
				return c->pool + c->ent[i].off;
			}
			c->ent[i].slot = -1;               /* geometry changed */
		}
		if (free_i < 0 && c->ent[i].slot < 0)
			free_i = i;
	}
	if (free_i < 0 || c->pool_used + need > c->pool_words) {
		c->full++;
		return NULL;
	}
	c->ent[free_i].slot  = slot;
	c->ent[free_i].idx   = idx;
	c->ent[free_i].w     = w;
	c->ent[free_i].h     = h;
	pt_set_used(&c->ent[free_i], used, nused);
	c->ent[free_i].epoch = pt_epoch(rm_row, c->ent[free_i].uidx,
	                                c->ent[free_i].nused);
	c->ent[free_i].off   = c->pool_used;
	c->pool_used += need;
	c->miss++;
	return c->pool + c->ent[free_i].off;
}

#endif /* PLATFORM_PLANAR_TILE_CACHE_H */
