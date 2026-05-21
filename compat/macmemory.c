/*
 * Mac Memory Manager shim — implementation (ADR-0003).
 *
 * NewPtr / DisposePtr map onto the C heap; the m68k-atari-mint C library
 * backs malloc with GEMDOS allocation from the TPA, a faithful stand-in for
 * the Mac Memory Manager's non-relocatable blocks.
 *
 * Handles (relocatable blocks) are layered on top — see the Handle section
 * below. The C heap never relocates, so a handle's master pointer is simply
 * stable, and HLock / HUnlock are bookkeeping.
 */

#include <stdlib.h>
#include <string.h>
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

/*
 * --- relocatable blocks (Handle) ---
 *
 * A Mac Handle is a pointer to a master pointer, so the Memory Manager can
 * relocate a block by rewriting that one master pointer. The C heap does not
 * relocate, so here each handle is a small `handle_entry` whose first field
 * — the master pointer — is allocated once and never moves; HLock / HUnlock
 * are then mere bookkeeping. The master pointer is the first field, so a
 * Handle and its handle_entry share an address.
 */

#define lockBit 0x80u           /* handle-state flags: bit 7 = locked */

typedef struct {
	Ptr           block;    /* master pointer: the block (NULL if empty) */
	Size          size;     /* block size in bytes                       */
	unsigned char state;    /* Mac handle-state flags                    */
} handle_entry;

Handle NewHandle(Size byteCount)
{
	handle_entry *e;

	if (byteCount < 0)
		byteCount = 0;
	e = (handle_entry *)NewPtr((Size)sizeof(handle_entry));
	if (e == NULL)
		return NULL;                    /* NewPtr set memFullErr */
	e->block = NewPtr(byteCount);
	if (e->block == NULL && byteCount != 0) {
		DisposePtr((Ptr)e);             /* clears g_mem_error...     */
		g_mem_error = memFullErr;       /* ...so restore the error  */
		return NULL;
	}
	e->size  = byteCount;
	e->state = 0;
	return (Handle)e;
}

Handle NewHandleClear(Size byteCount)
{
	Handle h = NewHandle(byteCount);

	if (h != NULL && *h != NULL && byteCount > 0)
		memset(*h, 0, (size_t)byteCount);
	return h;
}

void DisposeHandle(Handle h)
{
	if (h == NULL)
		return;
	DisposePtr(*h);                 /* the block (free(NULL) is safe) */
	DisposePtr((Ptr)h);             /* the handle_entry               */
}

Size GetHandleSize(Handle h)
{
	return (h != NULL) ? ((handle_entry *)h)->size : 0;
}

void SetHandleSize(Handle h, Size newSize)
{
	handle_entry *e = (handle_entry *)h;
	Ptr nb;

	if (e == NULL)
		return;
	if (newSize < 0)
		newSize = 0;
	nb = (Ptr)realloc(e->block, (size_t)newSize);
	if (nb == NULL && newSize != 0) {
		g_mem_error = memFullErr;       /* the block is left unchanged */
		return;
	}
	e->block = nb;
	e->size  = newSize;
	g_mem_error = 0;
}

void HLock(Handle h)
{
	if (h != NULL)
		((handle_entry *)h)->state |= lockBit;
}

void HUnlock(Handle h)
{
	if (h != NULL)
		((handle_entry *)h)->state &= (unsigned char)~lockBit;
}

SignedByte HGetState(Handle h)
{
	return (h != NULL) ? (SignedByte)((handle_entry *)h)->state : 0;
}

void HSetState(Handle h, SignedByte state)
{
	if (h != NULL)
		((handle_entry *)h)->state = (unsigned char)state;
}
