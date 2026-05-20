/*
 * Mac Toolbox compatibility shim — umbrella header.
 *
 * The decompiled FRUA engine calls the Mac Toolbox as if it were still
 * running on a Macintosh. This shim implements that API surface on top of
 * the Atari platform HAL (see platform/include/). See docs/decisions.md
 * (ADR-0003) and docs/toolbox-mapping.md.
 *
 * As each Toolbox manager is implemented it gets its own header here
 * (quickdraw.h, resources.h, memory.h, sound.h, files.h, events.h, ...);
 * this umbrella pulls them together for the engine.
 */

#ifndef COMPAT_MACGLUE_H
#define COMPAT_MACGLUE_H

/* Per-manager headers are included here as they come online:
 *
 *   #include "quickdraw.h"
 *   #include "resources.h"
 *   #include "memory.h"
 *   #include "sound.h"
 *   #include "files.h"
 *   #include "events.h"
 */

/* Bring up the Toolbox shim. Call once after the platform HAL is
 * initialised and before any engine code runs. Returns 0 on success. */
int  macglue_init(void);

/* Tear the shim down; release everything macglue_init() acquired. */
void macglue_shutdown(void);

#endif /* COMPAT_MACGLUE_H */
