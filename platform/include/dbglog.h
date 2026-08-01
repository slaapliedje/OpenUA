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
void dbg_log_num(const char *label, long value);

/*
 * File-based variant — appends "label value\r\n" to C:\DBG.LOG (the GEMDOS
 * mount, i.e. data/work/gamedata/DBG.LOG on the host). Unlike dbg_log_num
 * (VT-52 console, needs Hatari --conout 2 which swallows the cursor arrows),
 * this works WITHOUT --conout 2 — so debug traces survive the no-conout mode
 * the harness uses for arrow-key input. Truncates on the first call per run.
 * A bring-up aid; not for shipping code.
 */
void dbg_file_num(const char *label, long value);
void dbg_file_str(const char *label, const char *value);

/*
 * Called by a display backend once it owns the framebuffer. After this,
 * dbg_log / dbg_log_num write to DBG.LOG instead of the VT-52 console —
 * Cconws renders into the logical screen base, which by then is one of the
 * engine's own pages, and the glyphs surface as a band of coloured fragments
 * across one text row. (Not a Hatari artefact: A/B recordings with and
 * without --conout 2 are pixel-identical.) The Amiga backend has no such
 * call; dbglog_amiga.c is file-backed throughout.
 */
void dbg_log_screen_owned(void);


#endif /* PLATFORM_DBGLOG_H */
