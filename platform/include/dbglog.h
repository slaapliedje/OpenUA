/*
 * Debug log — writes a line to the VT-52 console.
 *
 * Boot Hatari with `--conout 2` to mirror the console to the host terminal;
 * the trail is then captured even when the program crashes too fast to read
 * on screen. A bring-up aid; not for shipping code.
 */

#ifndef PLATFORM_DBGLOG_H
#define PLATFORM_DBGLOG_H

void dbg_log(const char *msg);

#endif /* PLATFORM_DBGLOG_H */
