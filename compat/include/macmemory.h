/*
 * Mac Memory Manager shim — non-relocatable blocks (Ptr).
 *
 * Part of the compat/ Toolbox shim (ADR-0003). The decompiled engine calls
 * the Mac Memory Manager as on the original; on the Atari this is backed by
 * the C heap. See compat/macmemory.c.
 *
 * The Ptr (non-relocatable) API is here, plus the Handle type — other shims
 * need it for struct fields. The Handle *API* (NewHandle, the handle table)
 * is still to do — see docs/toolbox-mapping.md.
 */

#ifndef COMPAT_MACMEMORY_H
#define COMPAT_MACMEMORY_H

typedef char  *Ptr;     /* pointer to a non-relocatable block */
typedef Ptr   *Handle;  /* handle to a relocatable block      */
typedef long   Size;    /* a byte count                      */
typedef short  OSErr;   /* Toolbox error code                */

Ptr   NewPtr(Size byteCount);   /* allocate a block; NULL on failure */
void  DisposePtr(Ptr p);        /* free a NewPtr block               */
OSErr MemError(void);           /* error code from the most recent call */
Size  FreeMem(void);            /* free memory available             */

#endif /* COMPAT_MACMEMORY_H */
