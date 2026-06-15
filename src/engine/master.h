/*
 * FRUA master init / shutdown — see master.c.
 *
 * Lifted from CODE 5 (jump-table entries 1079 and 1081): the Toolbox +
 * display + file-cache bring-up main() runs once the screen mode is chosen,
 * and its matching teardown.
 */

#ifndef ENGINE_MASTER_H
#define ENGINE_MASTER_H

/*
 * Bring up the application: Mac Toolbox init, the offscreen drawing pages,
 * and the GLIB FAR pool (glib_pool_open, sized between kb_min and kb_max KB
 * — the lifted JT[463]). arg1 / arg2 are passed through from ua_main; their
 * meaning is still to be traced.
 */
void master_init(short arg1, long arg2, short kb_min, short kb_max);

/*
 * Tear the application back down — the teardown counterpart of master_init:
 * Mac Toolbox shutdown, the offscreen pages, and glib_pool_close (JT[466]).
 */
void master_shutdown(void);

#endif /* ENGINE_MASTER_H */
