/*
 * Mac Memory Manager shim — Ptr and Handle blocks.
 *
 * Part of the compat/ Toolbox shim (ADR-0003). The decompiled engine calls
 * the Mac Memory Manager as on the original; on the Atari this is backed by
 * the C heap. See compat/macmemory.c.
 *
 * Both sides are here: the Ptr (non-relocatable) API and the Handle
 * (relocatable) API. The Atari C heap does not relocate, so a Handle's
 * master pointer is allocated once and never moves — HLock / HUnlock become
 * bookkeeping.
 */

#ifndef COMPAT_MACMEMORY_H
#define COMPAT_MACMEMORY_H

typedef char       *Ptr;        /* pointer to a non-relocatable block */
typedef Ptr        *Handle;     /* handle to a relocatable block      */
typedef long        Size;       /* a byte count                       */
typedef short       OSErr;      /* Toolbox error code                 */
typedef signed char SignedByte; /* a Mac signed byte                  */

/* --- non-relocatable blocks (Ptr) --- */
Ptr   NewPtr(Size byteCount);   /* allocate a block; NULL on failure    */
void  DisposePtr(Ptr p);        /* free a NewPtr block                  */
OSErr MemError(void);           /* error code from the most recent call */
Size  FreeMem(void);            /* free memory available                */

/* --- relocatable blocks (Handle) --- */
Handle     NewHandle(Size byteCount);       /* allocate; NULL on failure  */
Handle     NewHandleClear(Size byteCount);  /* allocate and zero          */
void       DisposeHandle(Handle h);         /* free a NewHandle block     */
Size       GetHandleSize(Handle h);
void       SetHandleSize(Handle h, Size newSize);  /* resize the block    */
void       HLock(Handle h);                 /* mark the block locked      */
void       HUnlock(Handle h);               /* mark it unlocked           */
SignedByte HGetState(Handle h);             /* the handle-state flags     */
void       HSetState(Handle h, SignedByte state);

#endif /* COMPAT_MACMEMORY_H */
