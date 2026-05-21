/*
 * Mac Resource Manager shim — FRSC archive reader. See resources.h.
 *
 * The FRSC archive (format in tools/README.md) is a 16-byte header, an entry
 * table of N x 16 bytes sorted by (type, id), and the resource bodies
 * concatenated. All fields are big-endian; they are read byte-wise here, so
 * the parse does not depend on the archive's alignment in memory.
 *
 * GetResource loads a resource body into a Handle and remembers it, so a
 * second GetResource of the same resource returns the same Handle, as on the
 * Mac. Resource attributes (preload, locked, ...) are not honoured yet.
 */

#include <stddef.h>             /* NULL           */
#include <string.h>             /* memcpy, memset */

#include "resources.h"
#include "macmemory.h"          /* NewHandle, DisposeHandle, NewPtr, ... */

#define resNotFound  (-192)     /* Resource Manager: resource not found */
#define memFullErr   (-108)     /* Memory Manager: out of memory        */

#define FRSC_ENTRY_SIZE  16     /* bytes per entry-table entry */

static const unsigned char *g_arch;     /* the FRSC archive base          */
static const unsigned char *g_entries;  /* the entry table                */
static short                g_count;    /* number of entries              */
static Handle              *g_loaded;   /* per-entry loaded Handle, or NULL */
static OSErr                g_res_error;

/* Read a big-endian 16-/32-bit field. */
static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

static unsigned long be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

int resource_open(const void *archive)
{
	const unsigned char *a = (const unsigned char *)archive;
	long tbl;
	Size loaded_bytes;

	g_arch = NULL;
	if (a == NULL || a[0] != 'F' || a[1] != 'R' || a[2] != 'S'
	    || a[3] != 'C' || be16(a + 4) != 1) {
		g_res_error = resNotFound;
		return -1;
	}
	g_count = (short)be16(a + 6);
	tbl     = (long)be32(a + 8);

	loaded_bytes = (Size)g_count * (Size)sizeof(Handle);
	g_loaded = (Handle *)NewPtr(loaded_bytes);
	if (g_loaded == NULL && g_count != 0) {
		g_res_error = memFullErr;
		return -1;
	}
	if (g_loaded != NULL)
		memset(g_loaded, 0, (size_t)loaded_bytes);

	g_arch      = a;
	g_entries   = a + tbl;
	g_res_error = 0;
	return 0;
}

/* Binary-search the entry table for (type, id); the table is sorted. */
static short res_find(ResType type, short id)
{
	short lo = 0, hi = (short)(g_count - 1);

	while (lo <= hi) {
		short                mid = (short)(lo + (hi - lo) / 2);
		const unsigned char *e   = g_entries + (long)mid * FRSC_ENTRY_SIZE;
		ResType              et  = (ResType)be32(e);
		short                ei  = (short)be16(e + 4);

		if (et < type || (et == type && ei < id))
			lo = (short)(mid + 1);
		else if (et > type || (et == type && ei > id))
			hi = (short)(mid - 1);
		else
			return mid;
	}
	return -1;
}

/* Load entry `idx` into a Handle, caching it; returns the Handle or NULL. */
static Handle res_load(short idx)
{
	const unsigned char *e;
	Handle h;
	long   off, len;

	if (g_loaded != NULL && g_loaded[idx] != NULL) {
		g_res_error = 0;
		return g_loaded[idx];
	}
	e   = g_entries + (long)idx * FRSC_ENTRY_SIZE;
	off = (long)be32(e + 8);
	len = (long)be32(e + 12);
	h   = NewHandle((Size)len);
	if (h == NULL) {
		g_res_error = memFullErr;
		return NULL;
	}
	if (len > 0)
		memcpy(*h, g_arch + off, (size_t)len);
	if (g_loaded != NULL)
		g_loaded[idx] = h;
	g_res_error = 0;
	return h;
}

Handle GetResource(ResType type, short id)
{
	short idx;

	if (g_arch == NULL) {
		g_res_error = resNotFound;
		return NULL;
	}
	idx = res_find(type, id);
	if (idx < 0) {
		g_res_error = resNotFound;
		return NULL;
	}
	return res_load(idx);
}

Handle GetIndResource(ResType type, short index)
{
	short i, n = 0;

	if (g_arch != NULL && index > 0) {
		for (i = 0; i < g_count; i++) {
			const unsigned char *e = g_entries + (long)i * FRSC_ENTRY_SIZE;

			if ((ResType)be32(e) == type && ++n == index)
				return res_load(i);
		}
	}
	g_res_error = resNotFound;
	return NULL;
}

short CountResources(ResType type)
{
	short i, n = 0;

	for (i = 0; i < g_count; i++) {
		if ((ResType)be32(g_entries + (long)i * FRSC_ENTRY_SIZE) == type)
			n++;
	}
	g_res_error = 0;
	return n;
}

long SizeResource(Handle h)
{
	g_res_error = 0;
	return GetHandleSize(h);
}

void ReleaseResource(Handle h)
{
	short i;

	if (h != NULL && g_loaded != NULL) {
		for (i = 0; i < g_count; i++) {
			if (g_loaded[i] == h) {
				g_loaded[i] = NULL;
				break;
			}
		}
	}
	DisposeHandle(h);
	g_res_error = 0;
}

OSErr ResError(void)
{
	return g_res_error;
}
