/*
 * Amiga NDK includes for translation units that ALSO see the Mac Toolbox
 * shim headers (ADR-0012).
 *
 * The NDK's graphics `Point` (graphics/gfx.h, pulled in transitively by the
 * proto/inline headers — proto/dos.h -> inline/dos.h -> inline/stubs.h ->
 * graphics/displayinfo.h) collides with QuickDraw's `Point`, and they are
 * genuinely different structs (Mac {v,h} shorts vs Amiga {x,y} WORDs). Rename
 * the NDK's for this TU; nothing that includes this wrapper uses the Amiga
 * struct under that name. Include this BEFORE any Mac shim header, so the
 * rename window closes before QuickDraw's Point is declared.
 *
 * Pure-platform TUs (the platform/amiga sources) that never see the shim
 * headers can keep including the NDK directly.
 */

#ifndef PLATFORM_AMIGA_NDK_H
#define PLATFORM_AMIGA_NDK_H

#ifdef FRUA_AMIGA

/* dos.library ONLY. proto/exec.h both macro-defines AND declares FreeMem
 * (ptr, size), which collides irreconcilably with the Mac Memory Manager's
 * FreeMem(void) — and no shim-side TU calls exec directly (allocation is the
 * pure-platform backends' business, and the Mac spellings route through
 * macmemory.c). If a future shim TU genuinely needs an exec call, add a
 * rename window for it here the way Point gets one. */
#define Point AMIGA_NDK_Point
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#undef Point

#endif /* FRUA_AMIGA */

#endif /* PLATFORM_AMIGA_NDK_H */
