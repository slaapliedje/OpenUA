/*
 * Mac File Manager shim — see files.h.
 *
 * Each Mac entry is a thin wrapper over the matching GEMDOS call. The
 * GEMDOS file handle (a positive 16-bit value) doubles as the Mac
 * refNum, so close / read / write etc. pass it through unchanged. A few
 * pieces of slack we accept while the shim is young:
 *   - vRefNum is ignored; the engine runs out of one working directory.
 *   - HFS path syntax ("Vol:Folder:file") collapses to the trailing
 *     filename — anything before the last ':' is dropped because GEMDOS
 *     can't navigate Mac volume / folder names anyway.
 *   - The FInfo metadata (type, creator, flags) lives in a small in-shim
 *     cache keyed by filename — GEMDOS has no Finder info, but the
 *     engine relies on round-tripping the bytes it wrote.
 *   - FlushVol is a no-op: GEMDOS writes synchronously.
 *
 * GEMDOS errors map to the nearest Mac OSErr; everything else becomes
 * ioErr so the engine sees a non-zero return.
 */

#include <stddef.h>             /* NULL                  */
#include <string.h>             /* strncmp, memset       */
#include <mint/osbind.h>        /* GEMDOS Fopen / Fread / ... */

#include "files.h"

/* GEMDOS file-open modes. */
#define FOPEN_RD  0
#define FOPEN_WR  1
#define FOPEN_RW  2

/* GEMDOS Fseek modes. */
#define FSEEK_SET 0
#define FSEEK_CUR 1
#define FSEEK_END 2

#define MAX_PATH    256

/* Map a GEMDOS error code (negative) to a Mac OSErr. Positive GEMDOS
 * returns (the success path) come through as noErr at the call site. */
static OSErr gemdos_err(long r)
{
	switch (r) {
	case -33: return fnfErr;        /* EFILNF */
	case -34: return fnfErr;        /* EPTHNF — close enough for the engine */
	case -35: return tmfoErr;       /* ENHNDL */
	case -36: return wPrErr;        /* EACCDN */
	case -37: return paramErr;      /* EIHNDL */
	case -73: return wPrErr;        /* EWRPRO */
	case -79: return dupFNErr;      /* EFILMS — file exists */
	default:  return ioErr;
	}
}

/*
 * Translate a Mac Pascal filename into a C string suitable for GEMDOS:
 * drop everything up to and including the last ':' (HFS volume / folder
 * prefix the engine sometimes prepends), copy the rest, null-terminate.
 * The leading colon on a relative HFS path is allowed and dropped too.
 */
static void mac_path_to_c(ConstStr255Param p, char *out, int max)
{
	int len = p ? p[0] : 0;
	int start = 1;
	int i, j;

	for (i = len; i >= 1; i--) {
		if (p[i] == ':') {
			start = i + 1;
			break;
		}
	}
	j = 0;
	for (i = start; i <= len && j < max - 1; i++)
		out[j++] = (char)p[i];
	out[j] = '\0';
}

/* --- Finder info cache ---
 *
 * A small per-filename table holding the Finder bytes (type / creator /
 * flags / location / folder). The engine writes Finder info via SetFInfo
 * and reads it back via GetFInfo; on the Mac it round-trips through HFS,
 * but GEMDOS doesn't carry that metadata, so the shim keeps its own copy.
 * Lost on shutdown, which is fine — the engine treats FInfo as advisory.
 */
#define FINFO_CACHE 16
typedef struct {
	char  name[MAX_PATH];
	FInfo info;
	int   used;
} finfo_slot_t;

static finfo_slot_t g_finfo[FINFO_CACHE];

static finfo_slot_t *finfo_find(const char *name)
{
	int i;
	for (i = 0; i < FINFO_CACHE; i++)
		if (g_finfo[i].used && strncmp(g_finfo[i].name, name, MAX_PATH) == 0)
			return &g_finfo[i];
	return NULL;
}

static finfo_slot_t *finfo_alloc(const char *name)
{
	int i, j;

	for (i = 0; i < FINFO_CACHE; i++) {
		if (!g_finfo[i].used) {
			g_finfo[i].used = 1;
			for (j = 0; j < MAX_PATH - 1 && name[j]; j++)
				g_finfo[i].name[j] = name[j];
			g_finfo[i].name[j] = '\0';
			return &g_finfo[i];
		}
	}
	/* Reuse the first slot when full — the engine's working set is
	 * small; if this ever bites we'll grow the cache. */
	return &g_finfo[0];
}

/* --- the API --- */

OSErr FSOpen(ConstStr255Param fileName, short vRefNum, short *refNum)
{
	char path[MAX_PATH];
	long h;

	(void)vRefNum;
	if (fileName == NULL || refNum == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	h = Fopen(path, FOPEN_RW);
	if (h < 0)
		h = Fopen(path, FOPEN_RD);    /* fall back to read-only */
	if (h < 0)
		return gemdos_err(h);
	*refNum = (short)h;
	return noErr;
}

OSErr FSClose(short refNum)
{
	long r = Fclose(refNum);
	return r < 0 ? gemdos_err(r) : noErr;
}

OSErr FSRead(short refNum, long *count, void *buffPtr)
{
	long r;

	if (count == NULL || buffPtr == NULL)
		return paramErr;
	r = Fread(refNum, *count, buffPtr);
	if (r < 0) {
		*count = 0;
		return gemdos_err(r);
	}
	if (r < *count) {
		*count = r;
		return eofErr;                /* short read = end-of-file hit */
	}
	*count = r;
	return noErr;
}

OSErr FSWrite(short refNum, long *count, const void *buffPtr)
{
	long r;

	if (count == NULL || buffPtr == NULL)
		return paramErr;
	r = Fwrite(refNum, *count, buffPtr);
	if (r < 0) {
		*count = 0;
		return gemdos_err(r);
	}
	*count = r;
	return r < *count ? ioErr : noErr;
}

OSErr GetEOF(short refNum, long *logEOF)
{
	long here, end;

	if (logEOF == NULL)
		return paramErr;
	here = Fseek(0, refNum, FSEEK_CUR);
	if (here < 0)
		return gemdos_err(here);
	end = Fseek(0, refNum, FSEEK_END);
	if (end < 0)
		return gemdos_err(end);
	(void)Fseek(here, refNum, FSEEK_SET);
	*logEOF = end;
	return noErr;
}

OSErr SetEOF(short refNum, long logEOF)
{
	/* GEMDOS has no truncate-to-length; seek there and let the next
	 * write extend the file. Engine code uses SetEOF primarily to grow
	 * a file before random-access writes, which this satisfies. */
	long r = Fseek(logEOF, refNum, FSEEK_SET);

	return r < 0 ? gemdos_err(r) : noErr;
}

OSErr GetFPos(short refNum, long *filePos)
{
	long r;

	if (filePos == NULL)
		return paramErr;
	r = Fseek(0, refNum, FSEEK_CUR);
	if (r < 0)
		return gemdos_err(r);
	*filePos = r;
	return noErr;
}

OSErr SetFPos(short refNum, short posMode, long posOff)
{
	short  mode;
	long   r;

	switch (posMode) {
	case fsAtMark:    return noErr;                 /* nothing to do      */
	case fsFromStart: mode = FSEEK_SET; break;
	case fsFromLEOF:  mode = FSEEK_END; break;
	case fsFromMark:  mode = FSEEK_CUR; break;
	default:          return paramErr;
	}
	r = Fseek(posOff, refNum, mode);
	return r < 0 ? gemdos_err(r) : noErr;
}

OSErr Create(ConstStr255Param fileName, short vRefNum,
             OSType creator, OSType fileType)
{
	char            path[MAX_PATH];
	long            r;
	finfo_slot_t   *slot;

	(void)vRefNum;
	if (fileName == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	r = Fcreate(path, 0);                 /* attr 0 = normal file */
	if (r < 0)
		return gemdos_err(r);
	(void)Fclose((short)r);               /* Mac Create doesn't leave it open */

	slot = finfo_find(path);
	if (slot == NULL)
		slot = finfo_alloc(path);
	memset(&slot->info, 0, sizeof slot->info);
	slot->info.fdType    = fileType;
	slot->info.fdCreator = creator;
	return noErr;
}

OSErr FSDelete(ConstStr255Param fileName, short vRefNum)
{
	char           path[MAX_PATH];
	long           r;
	finfo_slot_t  *slot;

	(void)vRefNum;
	if (fileName == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	r = Fdelete(path);
	if (r < 0)
		return gemdos_err(r);
	slot = finfo_find(path);
	if (slot != NULL)
		slot->used = 0;
	return noErr;
}

OSErr GetFInfo(ConstStr255Param fileName, short vRefNum, FInfo *fndrInfo)
{
	char           path[MAX_PATH];
	finfo_slot_t  *slot;

	(void)vRefNum;
	if (fileName == NULL || fndrInfo == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);

	/* Existence: Fsfirst (search) is the GEMDOS analog; Fopen-then-Fclose
	 * is simpler and gives a definite answer. */
	{
		long h = Fopen(path, FOPEN_RD);
		if (h < 0)
			return gemdos_err(h);
		(void)Fclose((short)h);
	}
	slot = finfo_find(path);
	if (slot != NULL)
		*fndrInfo = slot->info;
	else
		memset(fndrInfo, 0, sizeof *fndrInfo);
	return noErr;
}

OSErr SetFInfo(ConstStr255Param fileName, short vRefNum,
               const FInfo *fndrInfo)
{
	char           path[MAX_PATH];
	finfo_slot_t  *slot;

	(void)vRefNum;
	if (fileName == NULL || fndrInfo == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	slot = finfo_find(path);
	if (slot == NULL)
		slot = finfo_alloc(path);
	slot->info = *fndrInfo;
	return noErr;
}

/* GetVol / SetVol — the engine reads / sets the default working directory.
 * We don't track a Mac-style volume name; reads return an empty Pascal
 * string and vRefNum 0; writes are accepted and ignored. */
OSErr GetVol(unsigned char *volName, short *vRefNum)
{
	if (volName != NULL)
		volName[0] = 0;
	if (vRefNum != NULL)
		*vRefNum = 0;
	return noErr;
}

OSErr SetVol(ConstStr255Param volName, short vRefNum)
{
	(void)volName;
	(void)vRefNum;
	return noErr;
}

OSErr FlushVol(ConstStr255Param volName, short vRefNum)
{
	(void)volName;
	(void)vRefNum;
	return noErr;                         /* GEMDOS writes synchronously */
}

/* --- directory enumeration ---
 *
 * The Mac File Manager walks a folder with indexed PBGetCatInfo; the
 * faithful roster scanner (JT[589]) needs that to list saved-character
 * files. GEMDOS does it with Fsfirst / Fsnext over a DTA (set by Fsetdta).
 * files_find_first(pattern, ...) starts a scan ("CHAR*.CHR" etc.) and
 * files_find_next continues it; each fills `out` with the matched 8.3
 * filename (a C string). Returns 1 while a name was found, 0 at the end.
 * One scan at a time (the DTA is shared) — fine for the roster load. */
static _DTA g_find_dta;

static void copy_dta_name(char *out, int max)
{
	int i;
	for (i = 0; i < max - 1 && g_find_dta.dta_name[i]; i++)
		out[i] = g_find_dta.dta_name[i];
	out[i] = '\0';
}

int files_find_first(const char *pattern, char *out, int max)
{
	if (out == NULL || max < 1)
		return 0;
	Fsetdta(&g_find_dta);
	if (Fsfirst(pattern, 0) != 0L)        /* attr 0 = ordinary files */
		return 0;
	copy_dta_name(out, max);
	return 1;
}

int files_find_next(char *out, int max)
{
	if (out == NULL || max < 1)
		return 0;
	if (Fsnext() != 0L)
		return 0;
	copy_dta_name(out, max);
	return 1;
}
