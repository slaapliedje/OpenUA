/*
 * FRUA core initialisation — lifted from CODE 6 (and CODE 5's DB arena).
 *
 *   core_init       L4cc0  — main()'s phase-2 core initialisation
 *   db_init         CODE 5 + 0x2822 (jump-table entry 1002, "DBInit")
 *   db_base         CODE 5 + 0x2850 (jump-table entry 1004)
 *   ua_arena_alloc  L531e  — bump allocator over the DB arena
 *
 * core_init() builds the engine's working storage: it sizes the DB arena,
 * allocates three fixed working buffers through the Memory Manager, and
 * fills seven (count, elem_size, base) data-table descriptors. The seven
 * per-table helpers L30cc / L30f4 / L311c / L3144 / L3154 / L317c / L31a4
 * are folded into core_alloc_table_far / core_alloc_table_arena here — they
 * differ only in which descriptor, element size, and backing allocator they
 * use; L3144 is the lone exception (it only aliases an existing table).
 *
 * Four routines core_init() calls live in segments not lifted yet; they are
 * the no-op stubs below, marked TODO (the fc_dump no-op-stub pattern). See
 * docs/decompilation.md for the application main() map.
 */

#include <stddef.h>            /* NULL */

#include "core.h"
#include "boot.h"             /* jt442 — real DLInit, was a local stub */
#include "alloc.h"             /* ua_alloc, ua_alloc_long */
#include "error.h"             /* ua_error */

/* --- DB arena: the CODE 5 globals behind db_init / db_base / L531e --- */
static void          *g_db_base;        /* A5-4582  — DB arena base       */
static long           g_db_size;        /* A5-4576  — DB arena size       */
static unsigned char *g_arena_cursor;   /* A5-17450 — L531e bump cursor   */

/*
 * Working storage core_init() fills. Kept file-private until the routines
 * that consume them are lifted — at which point they (and meaningful names)
 * move to core.h. The comments record what the disassembly proves: the A5
 * offset, the allocation size, and, for the tables, the geometry.
 */
static unsigned char *g_buf_1024;       /* A5-28006 — ua_alloc(1024), less 1 */
static void          *g_buf_2064;       /* A5-27944 — ua_alloc(2064)         */
static void          *g_buf_4590;       /* A5-27920 — ua_alloc(4590)         */
static void          *g_arena_2738;     /* A5-22306 — ua_arena_alloc(2738)   */
static void          *g_arena_1260;     /* A5-25318 — ua_arena_alloc(1260)   */

static ua_table_t  g_tbl_398a;          /* A5-22212 —   8 x 398, Memory Mgr */
static ua_table_t  g_tbl_62;            /* A5-21508 — 640 x  62, Memory Mgr */
static ua_table_t  g_tbl_10;            /* A5-21152 — 400 x  10, Memory Mgr */
static ua_table_t  g_tbl_398b;          /* A5-21860 —  60 x 398, DB arena   */
static ua_table_t  g_tbl_34;            /* A5-20800 —  70 x  34, DB arena   */
static ua_table_t  g_tbl_26;            /* A5-20448 —  68 x  26, DB arena   */
static ua_table_t *g_tbl_62_alias;      /* A5-21156 — points at g_tbl_62    */

/* --- DB arena (CODE 5 jump-table entries 1002 / 1004, and L531e) --- */

/*
 * db_init — CODE 5 + 0x2822 (jump-table entry 1002, "DBInit").
 *
 * Allocates the DB arena: a single `size`-byte block that ua_arena_alloc()
 * then hands out in bump-allocated slices.
 */
void db_init(long size)
{
	g_db_base = ua_alloc_long(size);
	if (g_db_base == NULL)
		ua_error("Out of memory in DBInit");
	g_db_size = size;
}

/* db_base — CODE 5 + 0x2850 (jump-table entry 1004): the DB arena base. */
void *db_base(void)
{
	return g_db_base;
}

/*
 * ua_arena_alloc — L531e.
 *
 * Bump-allocates `n` bytes from the DB arena; the cursor is parked at the
 * arena base on first use. There is no matching free — the arena is carved
 * once at start-up and released only when the arena itself is disposed.
 */
void *ua_arena_alloc(short n)
{
	void *result;

	if (g_arena_cursor == NULL)
		g_arena_cursor = db_base();
	result = g_arena_cursor;
	g_arena_cursor += n;
	return result;
}

/* --- not-yet-lifted cross-segment routines core_init() calls --- */
/*
 * Stubbed as no-ops so core_init() reads in its true shape; each is replaced
 * when its segment is lifted (the fc_dump no-op-stub pattern).
 */
static void jt231(void)    { }            /* TODO: lift CODE 7 + 0x0004 */
static void jt211(void)    { }            /* TODO: lift CODE 7 + 0x57bc */
static void jt981(short n) { (void)n; }   /* TODO: lift CODE 5 + 0x113e */

/* L59ca: a one-line wrapper — JT[981](6656). */
static void l59ca(void)
{
	jt981(6656);
}

/* --- table-descriptor allocators --- */
/*
 * core_alloc_table_far / core_alloc_table_arena — the folded L30cc / L30f4 /
 * L311c / L3154 / L317c / L31a4. Each records the table geometry and
 * allocates count * elem_size bytes; the product is taken modulo 16 bits,
 * as the Mac build does (it pushes only the low word of a 32-bit multiply).
 */
static void core_alloc_table_far(ua_table_t *t, short count, short elem_size)
{
	t->count     = count;
	t->elem_size = elem_size;
	t->base      = ua_alloc((unsigned short)(count * elem_size));
}

static void core_alloc_table_arena(ua_table_t *t, short count, short elem_size)
{
	t->count     = count;
	t->elem_size = elem_size;
	t->base      = ua_arena_alloc((short)(count * elem_size));
}

/*
 * core_init — L4cc0.
 *
 * main()'s phase-2 initialisation: it sizes the DB arena, allocates the
 * fixed working buffers, and fills the data-table descriptors. Call order
 * follows the Mac build — db_init() must precede any ua_arena_alloc().
 */
void core_init(void)
{
	db_init(0x9400L);                          /* 37888-byte DB arena */
	jt442(40);

	g_buf_1024 = (unsigned char *)ua_alloc(1024) - 1;   /* 1-based base */
	jt231();
	g_buf_2064 = ua_alloc(2064);
	g_buf_4590 = ua_alloc(4590);

	core_alloc_table_far(&g_tbl_398a, 8, 398);          /* L30cc */
	core_alloc_table_far(&g_tbl_62, 640, 62);           /* L311c */
	g_tbl_62_alias = &g_tbl_62;                         /* L3144 */
	core_alloc_table_far(&g_tbl_10, 400, 10);           /* L3154 */

	jt211();
	l59ca();

	g_arena_2738 = ua_arena_alloc(2738);
	g_arena_1260 = ua_arena_alloc(1260);

	core_alloc_table_arena(&g_tbl_398b, 60, 398);       /* L30f4 */
	core_alloc_table_arena(&g_tbl_34, 70, 34);          /* L317c */
	core_alloc_table_arena(&g_tbl_26, 68, 26);          /* L31a4 */
}
