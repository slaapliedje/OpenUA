/*
 * FRUA master init — see master.c.
 *
 * Lifted from CODE 5 + 0x4 (jump-table entry 1079): the Toolbox + display +
 * file-cache bring-up that main() runs once the screen mode is chosen.
 */

#ifndef ENGINE_MASTER_H
#define ENGINE_MASTER_H

/*
 * Bring up the application: Mac Toolbox init, the offscreen drawing pages,
 * and the file cache (fc_init between kb_min and kb_max KB). arg1 / arg2 are
 * passed through from ua_main; their meaning is still to be traced.
 */
void master_init(short arg1, long arg2, short kb_min, short kb_max);

#endif /* ENGINE_MASTER_H */
