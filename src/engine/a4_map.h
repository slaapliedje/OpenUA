/*
 * The A4 (STRS) pointer slots of the A5 world — see ADR-0017.
 *
 * Each entry records that the global at A5-offset `slot` points at the string
 * starting at `strs_off` in the STRS pool. Positions only: the text itself is
 * the user's, loaded from their own data at runtime. Generated from the Mac
 * DATA + DREL resources by tools/a4map.py, which is where the derivation is
 * documented.
 *
 * boot_a5_seed_defaults() applies these when the DATA replay has not already
 * supplied them, which is what lets a build run with the replay disabled.
 */
#ifndef ENGINE_A4_MAP_H
#define ENGINE_A4_MAP_H

struct a4_slot {
	short slot;      /* A5-relative offset (negative) */
	short strs_off;  /* byte offset into the STRS pool */
};

extern const struct a4_slot g_a4_map[];
extern const short          g_a4_map_count;

/* A5-internal relocations — slots pointing at other A5-world locations.
 * `strs_off` is reused as the (negative) target A5 offset. Only below-A5
 * targets appear; the FRUA build has 7 that point ABOVE A5, which the port
 * cannot model, and a4map.py records those in a comment in the generated .c. */
extern const struct a4_slot g_a5int_map[];
extern const short          g_a5int_map_count;

/* DIAGNOSTIC (a4map.py --with-scalars): raw initialised values no relocation
 * covers. Copyrighted payload — never in a redistributable build. Used to
 * bisect the scalar half of the A5 world; see ADR-0017 and task #70. */
struct a4_run {
	short slot;    /* A5-relative offset (negative) */
	short len;     /* run length in bytes           */
	short blob;    /* start index into the blob     */
};

/* Scalar runs recoverable from the user's DOS CKIT.EXE — positions plus a
 * checksum, never values, so unlike g_a5_scalar_blob this is shippable.
 * See ADR-0017 and task #71. */
struct a4_dos_run {
	short          slot;   /* A5-relative offset (negative)   */
	short          len;    /* run length in bytes             */
	long           off;    /* byte offset into CKIT.EXE       */
	unsigned short sum;    /* 16-bit sum of the run's bytes   */
	unsigned short flags;  /* A4_DOS_* treatment of the read  */
};

/* The DOS build stores tables in x86 byte order; reverse each scalar after
 * reading so the A5 world gets the Mac big-endian order the engine indexes.
 * The byte SUM is order-invariant, so verification needs no special case.
 *
 *   SWAP16     every 16-bit word           (#68)
 *   SWAP32     every 32-bit long           — the per-class XP thresholds
 *                                            at A5-30212 (#67)
 *   SWAP16_Q   the LEADING word of each 4-byte record, trailing two bytes
 *              left alone — `struct { short base; char dice; char sides; }`,
 *              the racial age/HP quads at A5-30780 (#67). Blind pair
 *              swapping transposes dice/sides too and never matches.
 */
#define A4_DOS_SWAP16   1
#define A4_DOS_SWAP32   2
#define A4_DOS_SWAP16_Q 4

extern const struct a4_dos_run g_a5_dos_scalars[];
extern const short             g_a5_dos_scalar_count;

extern const unsigned char g_a5_scalar_blob[];
extern const struct a4_run g_a5_scalar_runs[];
extern const short         g_a5_scalar_run_count;

#endif /* ENGINE_A4_MAP_H */
