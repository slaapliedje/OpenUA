/*
 * Mac Resource Manager shim — FRSC archive reader (ADR-0003, ADR-0007).
 *
 * Atari filesystems have no resource fork, so the Mac resource fork is
 * repacked host-side into one flat (type, id) archive — the FRSC format,
 * specified in tools/README.md. This shim reads that archive and serves the
 * Resource Manager API (GetResource and friends); resources come back as
 * Handles, backed by the Memory Manager shim.
 *
 * Here so far: a read-only Resource Manager over an in-memory FRSC archive.
 * Writing resources, named lookups, resource attributes, and opening the
 * archive from a file (the File Manager) are follow-ups — see
 * docs/toolbox-mapping.md.
 */

#ifndef COMPAT_RESOURCES_H
#define COMPAT_RESOURCES_H

#include "macmemory.h"          /* Handle, OSErr */

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

#endif /* COMPAT_RESOURCES_H */
