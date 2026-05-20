/*
 * Mac Memory Manager shim — implementation (ADR-0003).
 *
 * NewPtr/DisposePtr map onto the C heap. The m68k-atari-mint C library
 * backs malloc with GEMDOS allocation from the TPA, so this is a faithful
 * stand-in for the Mac Memory Manager's non-relocatable blocks.
 */

#include <stdlib.h>
#include <mint/osbind.h>

#include "macmemory.h"

#define memFullErr (-108)       /* Mac Memory Manager: not enough room */

static OSErr g_mem_error;       /* result of the most recent call */

Ptr NewPtr(Size byteCount)
{
	Ptr p = (byteCount >= 0) ? (Ptr)malloc((size_t)byteCount) : NULL;
	g_mem_error = (p != NULL || byteCount == 0) ? 0 : memFullErr;
	return p;
}

void DisposePtr(Ptr p)
{
	free(p);
	g_mem_error = 0;
}

OSErr MemError(void)
{
	return g_mem_error;
}

/*
 * The Mac Memory Manager's _FreeMem reports total free heap space; the Atari
 * stand-in returns the largest free block (GEMDOS Malloc(-1)), which is the
 * more useful figure for sizing a single large allocation.
 */
Size FreeMem(void)
{
	return (Size)Malloc(-1L);
}
