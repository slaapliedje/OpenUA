/*
 * FRUA application entry point — see boot.c.
 *
 * Lifted from CODE 6 + 0x58a (jump-table entry 12): the target the THINK C
 * runtime hands off to once the A5/A4 world is built — FRUA's own main().
 */

#ifndef ENGINE_BOOT_H
#define ENGINE_BOOT_H

/*
 * FRUA's bootstrap. In the Mac build the THINK C runtime calls this as
 * JT[12]; arg1 / arg2 are the two values it passes — their meaning is still
 * to be traced. Returns 0.
 */
int ua_main(short arg1, long arg2);

#endif /* ENGINE_BOOT_H */
