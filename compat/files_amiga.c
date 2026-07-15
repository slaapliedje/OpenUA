/*
 * Mac File Manager shim — Amiga (dos.library) backend (ADR-0012).
 *
 * The Falcon/TT backend is compat/files.c (GEMDOS); this is its AmigaOS twin.
 * Exactly one compiles per MACHINE. Every Mac entry is a thin wrapper over the
 * matching dos.library call, returning the same Mac OSErr the engine expects.
 *
 * ONE structural difference from the GEMDOS backend: a GEMDOS handle is a small
 * positive 16-bit value, so files.c stores it straight in the Mac `refNum`. A
 * dos.library handle is a 32-bit BPTR, which does NOT fit in a short refNum — so
 * this backend keeps a small descriptor table mapping refNum (a table index) to
 * the live BPTR. The path translation (HFS "Vol:Folder:file" -> a plain path,
 * keeping a ".DSN" design-folder prefix) mirrors files.c's mac_path_to_c.
 *
 * The directory scan (files_find_first/_next — the design picker, roster scan,
 * save-slot glob) speaks the GEMDOS dialect the engine already uses: '*'/'?'
 * globs matched case-insensitively, attr 0x10 = "directories also match", and
 * an optional directory prefix in the pattern ('\' or '/'). It is implemented
 * over Lock + Examine/ExNext with a private matcher rather than dos pattern
 * calls, so the semantics are exactly files.c's.
 *
 * Status: complete dos.library implementation, UNVERIFIED on amiberry/hardware
 * until the display backend lands and the port can actually boot.
 */

#ifdef FRUA_AMIGA

#include <stddef.h>
#include <string.h>

#include "amiga_ndk.h"          /* NDK protos, Point-collision-safe wrapper */
#include "files.h"

/* dos.library Seek() modes (dos/dos.h spells them OFFSET_*). */
#define A_OFFSET_BEGINNING  (-1)
#define A_OFFSET_CURRENT    (0)
#define A_OFFSET_END        (1)

/* refNum descriptor table: index 1..MAX_FH-1 -> live BPTR (0 = free). refNum 0
 * is reserved (the Mac treats 0 as "no file"). */
#define MAX_FH 32
static BPTR s_fh[MAX_FH];

static short fh_alloc(BPTR b)
{
	short i;
	for (i = 1; i < MAX_FH; i++)
		if (s_fh[i] == 0) { s_fh[i] = b; return i; }
	return 0;   /* table full */
}
static BPTR fh_get(short refNum)
{
	if (refNum <= 0 || refNum >= MAX_FH) return 0;
	return s_fh[refNum];
}
static void fh_free(short refNum)
{
	if (refNum > 0 && refNum < MAX_FH) s_fh[refNum] = 0;
}

/* Map an IoErr() code to the nearest Mac OSErr, the way files.c's
 * gemdos_err does for GEMDOS negatives. Anything unrecognised is a plain
 * ioErr so the engine always sees a non-zero failure. */
static OSErr amiga_err(void)
{
	switch (IoErr()) {
	case ERROR_OBJECT_NOT_FOUND:
	case ERROR_DIR_NOT_FOUND:
	case ERROR_DEVICE_NOT_MOUNTED:      return fnfErr;
	case ERROR_OBJECT_EXISTS:           return dupFNErr;
	case ERROR_OBJECT_IN_USE:           return fLckdErr;
	case ERROR_DISK_WRITE_PROTECTED:
	case ERROR_WRITE_PROTECTED:         return wPrErr;
	case ERROR_SEEK_ERROR:              return posErr;
	case ERROR_INVALID_COMPONENT_NAME:
	case ERROR_BAD_STREAM_NAME:         return paramErr;
	case ERROR_NO_FREE_STORE:
	case ERROR_DISK_FULL:               return ioErr;
	default:                            return ioErr;
	}
}

/* Translate a Mac Pascal filename into a C path for dos.library. Mirrors
 * files.c mac_path_to_c: drop everything up to the last ':' EXCEPT a folder
 * component ending in ".DSN" (a design data folder), which is kept with an
 * AmigaDOS '/' separator. Two Amiga-specific twists vs the GEMDOS twin:
 *  - the separator is '/', not '\';
 *  - the engine's own path builders (e.g. savgam_path) emit literal '\'
 *    separators, which GEMDOS consumes verbatim — here they translate to
 *    '/' so "<design>.DSN\SavGamA.csv" resolves the same way. */
static char fa_lc(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
}

static int fa_folder_is_dsn(ConstStr255Param p, int start, int end)
{
	int s = end - 4;
	if (end - start < 4)
		return 0;
	return fa_lc(p[s]) == '.' && fa_lc(p[s + 1]) == 'd'
	    && fa_lc(p[s + 2]) == 's' && fa_lc(p[s + 3]) == 'n';
}

static void mac_path_to_c(ConstStr255Param p, char *out, int max)
{
	int len = p ? p[0] : 0;
	int lastcolon = 0, prevcolon = 0;
	int i, j, fstart;

	for (i = len; i >= 1; i--)
		if (p[i] == ':') { lastcolon = i; break; }
	for (i = lastcolon - 1; i >= 1; i--)
		if (p[i] == ':') { prevcolon = i; break; }
	fstart = prevcolon + 1;

	j = 0;
	if (lastcolon && fa_folder_is_dsn(p, fstart, lastcolon)) {
		for (i = fstart; i < lastcolon && j < max - 1; i++)
			out[j++] = (char)p[i];
		if (j < max - 1)
			out[j++] = '/';             /* AmigaDOS path separator */
	}
	for (i = (lastcolon ? lastcolon + 1 : 1); i <= len && j < max - 1; i++)
		out[j++] = (p[i] == '\\') ? '/' : (char)p[i];
	out[j] = '\0';
}

/* --- core file ops -------------------------------------------------------- */

OSErr FSOpen(ConstStr255Param fileName, short vRefNum, short *refNum)
{
	char path[256];
	BPTR b;
	short r;

	(void)vRefNum;
	if (refNum == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);

	/* Mac FSOpen = open an EXISTING file read/write. MODE_OLDFILE is exactly
	 * that (existing, shared, read+write); MODE_READWRITE would CREATE on
	 * absence, which FSOpen must not. */
	b = Open((CONST_STRPTR)path, MODE_OLDFILE);
	if (b == 0)
		return amiga_err();
	r = fh_alloc(b);
	if (r == 0) {
		Close(b);
		return tmfoErr;                 /* descriptor table full */
	}
	*refNum = r;
	return noErr;
}

OSErr FSClose(short refNum)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	Close(b);
	fh_free(refNum);
	return noErr;
}

OSErr FSRead(short refNum, long *count, void *buffPtr)
{
	BPTR b = fh_get(refNum);
	LONG n;

	if (!b) return rfNumErr;
	if (count == NULL || buffPtr == NULL)
		return paramErr;
	n = Read(b, buffPtr, *count);
	if (n < 0) {
		*count = 0;
		return amiga_err();
	}
	if (n < *count) {
		*count = n;
		return eofErr;                  /* short read = end-of-file hit */
	}
	*count = n;
	return noErr;
}

OSErr FSWrite(short refNum, long *count, const void *buffPtr)
{
	BPTR b = fh_get(refNum);
	LONG n;

	if (!b) return rfNumErr;
	if (count == NULL || buffPtr == NULL)
		return paramErr;
	n = Write(b, (APTR)buffPtr, *count);
	if (n < 0) {
		*count = 0;
		return amiga_err();
	}
	*count = n;
	return noErr;
}

OSErr GetFPos(short refNum, long *filePos)
{
	BPTR b = fh_get(refNum);
	LONG pos;

	if (!b) return rfNumErr;
	if (filePos == NULL)
		return paramErr;
	/* A zero-displacement seek returns the (unchanged) current position. */
	pos = Seek(b, 0, A_OFFSET_CURRENT);
	if (pos < 0)
		return amiga_err();
	*filePos = pos;
	return noErr;
}

OSErr SetFPos(short refNum, short posMode, long posOff)
{
	BPTR b = fh_get(refNum);
	LONG mode, r;

	if (!b) return rfNumErr;
	switch (posMode) {
	case fsAtMark:    return noErr;      /* offset ignored; stay at the mark */
	case fsFromStart: mode = A_OFFSET_BEGINNING; break;
	case fsFromLEOF:  mode = A_OFFSET_END;       break;
	case fsFromMark:  mode = A_OFFSET_CURRENT;   break;
	default:          return paramErr;
	}
	r = Seek(b, posOff, mode);
	if (r < 0)
		return amiga_err();
	return noErr;
}

OSErr GetEOF(short refNum, long *logEOF)
{
	BPTR b = fh_get(refNum);
	LONG here, end;

	if (!b) return rfNumErr;
	if (logEOF == NULL)
		return paramErr;
	/* Seek returns the PREVIOUS position: hop to the end (remembering where
	 * we were), then hop back (learning where the end was). */
	here = Seek(b, 0, A_OFFSET_END);
	if (here < 0)
		return amiga_err();
	end = Seek(b, here, A_OFFSET_BEGINNING);
	if (end < 0)
		return amiga_err();
	*logEOF = end;
	return noErr;
}

OSErr SetEOF(short refNum, long logEOF)
{
	BPTR b = fh_get(refNum);

	if (!b) return rfNumErr;
	if (SetFileSize(b, logEOF, A_OFFSET_BEGINNING) < 0)
		return amiga_err();
	return noErr;
}

OSErr Create(ConstStr255Param fileName, short vRefNum, OSType creator, OSType fileType)
{
	char path[256];
	BPTR b;

	(void)vRefNum; (void)creator; (void)fileType;
	mac_path_to_c(fileName, path, sizeof path);
	/* Mac Create makes an empty file (error if it exists — dupFNErr). dos
	 * MODE_NEWFILE truncates an existing file instead, so probe first. */
	b = Open((CONST_STRPTR)path, MODE_OLDFILE);
	if (b != 0) {
		Close(b);
		return dupFNErr;
	}
	b = Open((CONST_STRPTR)path, MODE_NEWFILE);
	if (b == 0)
		return amiga_err();
	Close(b);
	return noErr;
}

OSErr FSDelete(ConstStr255Param fileName, short vRefNum)
{
	char path[256];

	(void)vRefNum;
	mac_path_to_c(fileName, path, sizeof path);
	if (!DeleteFile((CONST_STRPTR)path))
		return amiga_err();
	return noErr;
}

OSErr DirCreate(short vRefNum, long parentDirID, ConstStr255Param directoryName, long *createdDirID)
{
	char path[256];
	BPTR l;

	(void)vRefNum; (void)parentDirID;
	if (createdDirID)
		*createdDirID = 0;
	mac_path_to_c(directoryName, path, sizeof path);
	l = CreateDir((CONST_STRPTR)path);
	if (l == 0)
		return amiga_err();
	UnLock(l);
	return noErr;
}

/* --- Finder info: no dos.library analogue; keep an in-shim cache exactly
 * like the GEMDOS backend (files.c). The engine treats FInfo as advisory,
 * so losing the cache on shutdown is fine. ------------------------------- */

#define FA_MAX_PATH 256
#define FINFO_CACHE 16
typedef struct {
	char  name[FA_MAX_PATH];
	FInfo info;
	int   used;
} finfo_slot_t;

static finfo_slot_t g_finfo[FINFO_CACHE];

static finfo_slot_t *finfo_find(const char *name)
{
	int i;
	for (i = 0; i < FINFO_CACHE; i++)
		if (g_finfo[i].used && strncmp(g_finfo[i].name, name, FA_MAX_PATH) == 0)
			return &g_finfo[i];
	return NULL;
}

static finfo_slot_t *finfo_alloc(const char *name)
{
	int i, j;

	for (i = 0; i < FINFO_CACHE; i++) {
		if (!g_finfo[i].used) {
			g_finfo[i].used = 1;
			for (j = 0; j < FA_MAX_PATH - 1 && name[j]; j++)
				g_finfo[i].name[j] = name[j];
			g_finfo[i].name[j] = '\0';
			return &g_finfo[i];
		}
	}
	/* Reuse the first slot when full — the engine's working set is small. */
	return &g_finfo[0];
}

OSErr GetFInfo(ConstStr255Param fileName, short vRefNum, FInfo *fndrInfo)
{
	char path[256];
	finfo_slot_t *s;

	(void)vRefNum;
	if (fndrInfo == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	s = finfo_find(path);
	if (s != NULL)
		*fndrInfo = s->info;
	else
		memset(fndrInfo, 0, sizeof *fndrInfo);
	return noErr;
}

OSErr SetFInfo(ConstStr255Param fileName, short vRefNum, const FInfo *fndrInfo)
{
	char path[256];
	finfo_slot_t *s;

	(void)vRefNum;
	if (fndrInfo == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);
	s = finfo_find(path);
	if (s == NULL)
		s = finfo_alloc(path);
	s->info = *fndrInfo;
	return noErr;
}

/* --- volumes (the engine runs from one working dir) ----------------------- */

OSErr GetVol(unsigned char *volName, short *vRefNum)
{
	if (volName) volName[0] = 0;
	if (vRefNum) *vRefNum = 0;
	return noErr;
}

OSErr GetVInfo(ConstStr255Param volName, short *vRefNum, long *freeBytes)
{
	/* dos Info() on a lock of the current dir -> volume free space. The
	 * InfoData block must be longword-aligned. Used by the save-disk
	 * space check (jt1142/jt1051). */
	static struct InfoData id __attribute__((aligned(4)));
	BPTR l;

	(void)volName;
	if (vRefNum) *vRefNum = 0;
	if (freeBytes == NULL)
		return noErr;

	*freeBytes = 0;
	l = Lock((CONST_STRPTR)"", ACCESS_READ);   /* "" = the current dir */
	if (l == 0)
		return amiga_err();
	if (Info(l, &id))
		*freeBytes = (long)(id.id_NumBlocks - id.id_NumBlocksUsed)
		           * (long)id.id_BytesPerBlock;
	UnLock(l);
	return noErr;
}

OSErr SetVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }
OSErr FlushVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }

/* --- directory enumeration (jt315 design picker, roster scan, save glob) ---
 *
 * The GEMDOS backend runs Fsfirst/Fsnext over a DTA; here it is a Lock +
 * Examine/ExNext walk with a private GEMDOS-dialect matcher:
 *   - '*' and '?' globs, matched CASE-INSENSITIVELY (Fsfirst is; and the
 *     engine's globs like "SavGam*.csv" count on finding "SAVGAMA.CSV");
 *   - attr 0x10 (FA_SUBDIR) = directories ALSO match; attr 0 = files only;
 *   - the pattern may carry a directory prefix ('\' or '/'), e.g.
 *     "HEIRS.DSN\SavGam*.csv" — the scan runs inside that directory.
 * One scan at a time, exactly like the single-DTA GEMDOS twin. */

static BPTR                        s_scan_lock;
static struct FileInfoBlock        s_fib __attribute__((aligned(4)));
static char                        s_scan_pat[64];
static int                         s_scan_attr;
static int                         s_scan_live;

/* Case-insensitive '*'/'?' glob match (the Fsfirst dialect). */
static int glob_match(const char *pat, const char *name)
{
	while (*pat != '\0') {
		if (*pat == '*') {
			while (*pat == '*')
				pat++;
			if (*pat == '\0')
				return 1;
			for (; *name != '\0'; name++)
				if (glob_match(pat, name))
					return 1;
			return 0;
		}
		if (*name == '\0')
			return 0;
		if (*pat != '?'
		    && fa_lc((unsigned char)*pat) != fa_lc((unsigned char)*name))
			return 0;
		pat++;
		name++;
	}
	return *name == '\0';
}

static void scan_close(void)
{
	if (s_scan_lock != 0) {
		UnLock(s_scan_lock);
		s_scan_lock = 0;
	}
	s_scan_live = 0;
}

/* Advance ExNext until an entry passes the glob + attr filter; copy its name
 * out. Returns 1 on a match, 0 (and closes the scan) when exhausted. */
static int scan_next_match(char *out, int max)
{
	while (ExNext(s_scan_lock, &s_fib)) {
		int is_dir = (s_fib.fib_DirEntryType > 0);
		int j;

		if (is_dir && !(s_scan_attr & 0x10))
			continue;               /* dirs only match when asked for */
		if (!glob_match(s_scan_pat, (const char *)s_fib.fib_FileName))
			continue;
		for (j = 0; j < max - 1 && s_fib.fib_FileName[j]; j++)
			out[j] = (char)s_fib.fib_FileName[j];
		out[j] = '\0';
		return 1;
	}
	scan_close();
	return 0;
}

int files_find_first_attr(const char *pattern, int attr, char *out, int max)
{
	char dir[192];
	const char *name;
	int  i, cut = -1;

	if (pattern == NULL || out == NULL || max < 1)
		return 0;
	scan_close();

	/* Split an optional directory prefix off the pattern ('\' or '/'). */
	for (i = 0; pattern[i] != '\0'; i++)
		if (pattern[i] == '\\' || pattern[i] == '/')
			cut = i;
	if (cut >= 0) {
		int n = (cut < (int)sizeof dir - 1) ? cut : (int)sizeof dir - 1;
		for (i = 0; i < n; i++)
			dir[i] = (pattern[i] == '\\') ? '/' : pattern[i];
		dir[n] = '\0';
		name = pattern + cut + 1;
	} else {
		dir[0] = '\0';                  /* "" locks the current dir */
		name = pattern;
	}
	for (i = 0; name[i] != '\0' && i < (int)sizeof s_scan_pat - 1; i++)
		s_scan_pat[i] = name[i];
	s_scan_pat[i] = '\0';
	s_scan_attr = attr;

	s_scan_lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
	if (s_scan_lock == 0)
		return 0;
	if (!Examine(s_scan_lock, &s_fib)) {    /* primes ExNext */
		scan_close();
		return 0;
	}
	s_scan_live = 1;
	return scan_next_match(out, max);
}

int files_find_first(const char *pattern, char *out, int max)
{
	return files_find_first_attr(pattern, 0, out, max);  /* attr 0 = files */
}

/* Whether the entry the last find_first/find_next landed on is a directory. */
int files_find_is_dir(void)
{
	return (s_fib.fib_DirEntryType > 0) ? 1 : 0;
}

int files_find_next(char *out, int max)
{
	if (out == NULL || max < 1 || !s_scan_live)
		return 0;
	return scan_next_match(out, max);
}

#endif /* FRUA_AMIGA */
