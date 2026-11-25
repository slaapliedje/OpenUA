/*
 * gbmaps.h — a Gold Box game's maps read as the game keeps them (ADR-0026
 * step (f)): a map out of its GEO library made into the FRUA area the
 * engine walks, and into the descriptor plane its scripts dispatch on.
 * Plain C99 with no engine, shim or platform headers, so it builds on the
 * host for tests/test_gbmaps.py.
 *
 * tools/gbgeo.py is the reference: load_maps() reads the same maps,
 * GbMap.to_frua().build() writes the same area file byte for byte, and
 * map_descriptor() the same descriptor. Its docstring and to_frua()'s
 * comments say why the edges and axes are what they are.
 */
#ifndef ENGINE_GBMAPS_H
#define ENGINE_GBMAPS_H

#include "gblib.h"

#define GBG_CELLS   784         /* the engine's map (GEO_MAX_CELLS)          */
#define GBG_GEO_MAX (12962 + GBG_CELLS * 6 - 3456)   /* a wide area's file   */
#define GBG_DESC_MAX (8 + GBG_CELLS)

#define GBG_ESHAPE -4           /* a member that is not a map this reads     */

typedef struct gbg_map {
	unsigned      id;                       /* the game's own map number    */
	unsigned char width, height;            /* x east, y south              */
	unsigned char planes[4 * GBG_CELLS];    /* four planes, row-major       */
} gbg_map;

/* Map `i` of the library (its id table's order) into `m`: a 1024-byte
 * member is a 16x16 Gen1 block, one with the Dark Queen's 8-byte header
 * (height, width, ...) is read at its own size. 0, or a GBL_E / GBG_E
 * code. */
int gbg_read_map(gbr_read_fn rd, void *ctx, const gbl_lib *lib, unsigned i, gbg_map *m);

/* A 16x16 Gen1 map block (1024 bytes of planes, or 1026 with a DOS DAX
 * block's little-endian length word in front: Champions and Death Knights
 * of Krynn, tools/gbgeo.py from_gen1_block) as map `id`. 0, or GBG_ESHAPE. */
int gbg_from_block(const unsigned char *raw, long len, unsigned id, gbg_map *m);

/* The map as a FRUA area file (GEOnnn.DAT) into `out` (GBG_GEO_MAX):
 * walls on its edges, stock wall art, entry point 0 at the first cell with
 * a way out. Returns the file's length. */
long gbg_geo(const gbg_map *m, unsigned char *out);

/* The map's descriptor (GBDnnn.DAT: "GBD1", width, height, map number,
 * `boot` script, the descriptor plane) into `out` (GBG_DESC_MAX). Returns
 * its length, or -1 when the map does not fit the header. */
long gbg_desc(const gbg_map *m, unsigned boot, unsigned char *out);

#endif
