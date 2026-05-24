/*
 * Mac Resource Manager shim — FRSC archive reader (ADR-0003, ADR-0007).
 *
 * Atari filesystems have no resource fork, so the Mac resource fork is
 * repacked host-side into one flat (type, id) archive — the FRSC format,
 * specified in tools/README.md. This shim reads that archive and serves the
 * Resource Manager API (GetResource and friends); resources come back as
 * Handles, backed by the Memory Manager shim.
 *
 * Here so far: a read-only Resource Manager over an in-memory FRSC archive
 * plus the resource-file machinery (OpenResFile / UseResFile / CurResFile /
 * CloseResFile / HomeResFile / CreateResFile). Up to four open resource
 * files can be tracked; all alias the single in-memory archive for now,
 * which matches the engine that opens its own resource fork and reads
 * resources back out of it. Writing resources, named lookups, resource
 * attributes, real multi-archive support, and OpenRF / file-backed
 * archive loading are follow-ups — see docs/toolbox-mapping.md.
 */

#ifndef COMPAT_RESOURCES_H
#define COMPAT_RESOURCES_H

#include "macmemory.h"          /* Handle, OSErr           */
#include "quickdraw.h"          /* ConstStr255Param        */

typedef unsigned long ResType;  /* a four-character resource type code */

/*
 * Point the Resource Manager at a FRSC archive already in memory. Returns 0
 * on success, -1 if the buffer is not a version-1 FRSC archive. Call once,
 * at start-up. (A file-opening OpenResFile follows with the File Manager.)
 */
int resource_open(const void *archive);

/*
 * Load resource (type, id); returns its Handle, or NULL if not found. The
 * resource stays loaded — a repeat call returns the same Handle, as on the
 * Mac.
 */
Handle GetResource(ResType type, short id);

/* The index-th resource of `type` (1-based), or NULL. */
Handle GetIndResource(ResType type, short index);

/* How many resources of `type` the archive holds. */
short CountResources(ResType type);

/* The size in bytes of a loaded resource. */
long SizeResource(Handle h);

/* Release a resource loaded by GetResource / GetIndResource. */
void ReleaseResource(Handle h);

/* The error code from the most recent Resource Manager call. */
OSErr ResError(void);

/* --- resource files ---
 *
 * The Mac tracks a current resource file (the topmost in a search chain).
 * GetResource looks in the current file first, then the file below it, all
 * the way to the System resource file. The shim doesn't yet split its
 * archive across files, so all open resource files alias the single
 * in-memory FRSC archive — UseResFile and friends are bookkeeping that
 * gives the engine the API surface it expects. Real multi-archive support
 * follows when an engine call demands it.
 *
 * Refnums are positive small integers. Closing a refnum frees it for
 * reuse; closing the current refnum clears the current selection.
 */
short OpenResFile(ConstStr255Param fileName);
short OpenRFPerm(ConstStr255Param fileName, short vRefNum, signed char permission);
void  UseResFile(short refNum);
short CurResFile(void);
void  CloseResFile(short refNum);
short HomeResFile(Handle theResource);

/* Create a resource fork on the file. Writing resources isn't supported
 * yet, so this is a noErr stub — the create-then-open pattern still
 * proceeds. */
void  CreateResFile(ConstStr255Param fileName);

#endif /* COMPAT_RESOURCES_H */
