/*
 * File Manager shim — Unix (SVR4) backend: AMIX (Amiga UNIX), and Atari
 * System V running AMIX binaries. The GEMDOS backend (files.c) and the
 * AmigaDOS one (files_amiga.c) are this file's twins; the Mac-facing
 * behaviour is the same, only the system calls differ.
 *
 * Two things a Unix file system does that TOS and AmigaDOS do not:
 *   - names are CASE-SENSITIVE, while the engine and the game data spell
 *     them any way (the Mac, TOS and AmigaDOS all fold case). A path that
 *     does not exist as spelt is resolved component by component against
 *     the directory listing, case-insensitively (ci_resolve);
 *   - '/' separates directories; the engine's '\' (DOS/TOS spelling, as in
 *     "HEIRS.DSN\SavGam*.csv") is mapped to it.
 *
 * Only structures whose fields are all 4 bytes wide are shared with libc
 * (stat, dirent, statvfs): see toolchain/sysv4-cc.
 */
#ifdef FRUA_UNIX

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "files.h"

/* refNum descriptor table: index 1..MAX_FH-1 -> fd + 1 (0 = free). refNum 0
 * is reserved (the Mac treats 0 as "no file"). */
#define MAX_FH 32
static int s_fh[MAX_FH];

static short fh_alloc(int fd)
{
	short i;
	for (i = 1; i < MAX_FH; i++)
		if (s_fh[i] == 0) { s_fh[i] = fd + 1; return i; }
	return 0;   /* table full */
}
static int fh_get(short refNum)
{
	if (refNum <= 0 || refNum >= MAX_FH) return -1;
	return s_fh[refNum] - 1;
}
static void fh_free(short refNum)
{
	if (refNum > 0 && refNum < MAX_FH) s_fh[refNum] = 0;
}

/* Map errno to the nearest Mac OSErr, the way files.c's gemdos_err does for
 * GEMDOS negatives. Anything unrecognised is a plain ioErr so the engine
 * always sees a non-zero failure. */
static OSErr unix_err(void)
{
	switch (errno) {
	case ENOENT:
	case ENOTDIR:	return fnfErr;
	case EEXIST:	return dupFNErr;
	case EBUSY:
	case ETXTBSY:	return fLckdErr;
	case EROFS:
	case EACCES:
	case EPERM:	return wPrErr;
	case ESPIPE:
	case EINVAL:	return posErr;
	case EMFILE:
	case ENFILE:	return tmfoErr;
	case ENOSPC:	return ioErr;	/* the shim has no dskFulErr */
	case ENAMETOOLONG: return ioErr;
	default:	return ioErr;
	}
}

static char fa_lc(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
}

static int ci_equal(const char *a, const char *b)
{
	while (*a != '\0' && fa_lc((unsigned char)*a) == fa_lc((unsigned char)*b))
		a++, b++;
	return *a == '\0' && *b == '\0';
}

/*
 * Resolve `in` (a relative or absolute '/' path) case-insensitively into
 * `out`. Components that exist as spelt are kept; the first that does not
 * is looked up in its directory with case folded, and so on. When nothing
 * matches, the remainder is kept as spelt (so a Create makes the name the
 * engine asked for). Always fills `out`.
 */
static void ci_resolve(const char *in, char *out, int max)
{
	struct stat st;
	int i = 0, o = 0;

	if (stat(in, &st) == 0 || max < 2) {	/* the common case */
		strncpy(out, in, max - 1);
		out[max - 1] = '\0';
		return;
	}
	if (in[0] == '/')
		out[o++] = in[i++];
	out[o] = '\0';
	while (in[i] != '\0' && o < max - 1) {
		char comp[256], dir[512];
		int c = 0, found = 0;
		DIR *d;

		while (in[i] != '\0' && in[i] != '/' && c < (int)sizeof comp - 1)
			comp[c++] = in[i++];
		comp[c] = '\0';
		/* the directory to search is what has been resolved so far */
		if (o == 0)
			strcpy(dir, ".");
		else {
			strncpy(dir, out, sizeof dir - 1);
			dir[sizeof dir - 1] = '\0';
		}
		d = opendir(dir);
		if (d != NULL) {
			struct dirent *e;
			while ((e = readdir(d)) != NULL)
				if (ci_equal(comp, e->d_name)) {
					strncpy(comp, e->d_name, sizeof comp - 1);
					found = 1;
					break;
				}
			closedir(d);
		}
		(void)found;			/* unmatched: keep the spelling */
		for (c = 0; comp[c] != '\0' && o < max - 1; c++)
			out[o++] = comp[c];
		if (in[i] == '/' && o < max - 1)
			out[o++] = in[i++];
		out[o] = '\0';
	}
}

static int fa_folder_is_dsn(ConstStr255Param p, int start, int end)
{
	int s = end - 4;
	if (end - start < 4)
		return 0;
	return fa_lc(p[s]) == '.' && fa_lc(p[s + 1]) == 'd'
	    && fa_lc(p[s + 2]) == 's' && fa_lc(p[s + 3]) == 'n';
}

/* Mac path (Pascal string) -> resolved Unix path. Only the last directory
 * is kept, and only when it is a design ("<name>.DSN:<file>"): the engine
 * runs from the game directory, exactly as on the other ports. */
static void mac_path_to_c(ConstStr255Param p, char *out, int max)
{
	char tmp[256];
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
		for (i = fstart; i < lastcolon && j < (int)sizeof tmp - 1; i++)
			tmp[j++] = (char)p[i];
		if (j < (int)sizeof tmp - 1)
			tmp[j++] = '/';
	}
	for (i = (lastcolon ? lastcolon + 1 : 1); i <= len && j < (int)sizeof tmp - 1; i++)
		tmp[j++] = (p[i] == '\\') ? '/' : (char)p[i];
	tmp[j] = '\0';
	ci_resolve(tmp, out, max);
}

/* --- core file ops -------------------------------------------------------- */

OSErr FSOpen(ConstStr255Param fileName, short vRefNum, short *refNum)
{
	char path[256];
	int fd;
	short r;

	(void)vRefNum;
	if (refNum == NULL)
		return paramErr;
	mac_path_to_c(fileName, path, sizeof path);

	/* Mac FSOpen = open an EXISTING file read/write. A read-only file (the
	 * installed game data may well be) still opens, for reading. */
	fd = open(path, O_RDWR);
	if (fd < 0 && (errno == EACCES || errno == EROFS))
		fd = open(path, O_RDONLY);
	if (fd < 0)
		return unix_err();
	r = fh_alloc(fd);
	if (r == 0) {
		close(fd);
		return tmfoErr;                 /* descriptor table full */
	}
	*refNum = r;
#ifdef FRUA_FILETRACE
	{
		extern void dbg_file_str(const char *, const char *);
		extern void dbg_file_num(const char *, long);
		dbg_file_str("ft: open ", path);
		dbg_file_num("ft: -> refnum ", (long)r);
	}
#endif
	return noErr;
}

OSErr FSClose(short refNum)
{
	int fd = fh_get(refNum);
	if (fd < 0) return rfNumErr;
	close(fd);
	fh_free(refNum);
	return noErr;
}

OSErr FSRead(short refNum, long *count, void *buffPtr)
{
	int fd = fh_get(refNum);
	long n;

	if (fd < 0) return rfNumErr;
	if (count == NULL || buffPtr == NULL)
		return paramErr;
	n = read(fd, buffPtr, (unsigned)*count);
	if (n < 0) {
		*count = 0;
		return unix_err();
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
	int fd = fh_get(refNum);
	long n;

	if (fd < 0) return rfNumErr;
	if (count == NULL || buffPtr == NULL)
		return paramErr;
	n = write(fd, buffPtr, (unsigned)*count);
	if (n < 0) {
		*count = 0;
		return unix_err();
	}
	*count = n;
	return noErr;
}

OSErr GetFPos(short refNum, long *filePos)
{
	int fd = fh_get(refNum);
	long pos;

	if (fd < 0) return rfNumErr;
	if (filePos == NULL)
		return paramErr;
	pos = lseek(fd, 0L, SEEK_CUR);
	if (pos < 0)
		return unix_err();
	*filePos = pos;
	return noErr;
}

OSErr SetFPos(short refNum, short posMode, long posOff)
{
	int fd = fh_get(refNum);
	int whence;

	if (fd < 0) return rfNumErr;
	switch (posMode) {
	case fsAtMark:    return noErr;      /* offset ignored; stay at the mark */
	case fsFromStart: whence = SEEK_SET; break;
	case fsFromLEOF:  whence = SEEK_END; break;
	case fsFromMark:  whence = SEEK_CUR; break;
	default:          return paramErr;
	}
	if (lseek(fd, posOff, whence) < 0)
		return unix_err();
	return noErr;
}

OSErr GetEOF(short refNum, long *logEOF)
{
	int fd = fh_get(refNum);
	struct stat st;

	if (fd < 0) return rfNumErr;
	if (logEOF == NULL)
		return paramErr;
	if (fstat(fd, &st) < 0)
		return unix_err();
	*logEOF = (long)st.st_size;
	return noErr;
}

OSErr SetEOF(short refNum, long logEOF)
{
	int fd = fh_get(refNum);
	struct flock fl;

	if (fd < 0) return rfNumErr;
	/* SVR4.0 truncates with F_FREESP: free from l_start to the end */
	memset(&fl, 0, sizeof fl);
	fl.l_whence = SEEK_SET;
	fl.l_start = logEOF;
	fl.l_len = 0;
	if (fcntl(fd, F_FREESP, &fl) < 0)
		return unix_err();
	return noErr;
}

OSErr Create(ConstStr255Param fileName, short vRefNum, OSType creator, OSType fileType)
{
	char path[256];
	int fd;

	(void)vRefNum; (void)creator; (void)fileType;
	mac_path_to_c(fileName, path, sizeof path);
	/* Mac Create makes an empty file, and fails (dupFNErr) if it exists */
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0)
		return unix_err();
	close(fd);
	return noErr;
}

OSErr FSDelete(ConstStr255Param fileName, short vRefNum)
{
	char path[256];

	(void)vRefNum;
	mac_path_to_c(fileName, path, sizeof path);
	if (unlink(path) < 0)
		return unix_err();
	return noErr;
}

OSErr DirCreate(short vRefNum, long parentDirID, ConstStr255Param directoryName, long *createdDirID)
{
	char path[256];

	(void)vRefNum; (void)parentDirID;
	if (createdDirID)
		*createdDirID = 0;
	mac_path_to_c(directoryName, path, sizeof path);
	if (mkdir(path, 0777) < 0)
		return unix_err();
	return noErr;
}

/* --- Finder info: no Unix analogue; keep an in-shim cache exactly like the
 * GEMDOS backend (files.c). The engine treats FInfo as advisory, so losing
 * the cache on shutdown is fine. ------------------------------------------ */

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
	/* free space of the file system holding the current directory: the
	 * save-disk space check (jt1142/jt1051) */
	struct statvfs sv;

	(void)volName;
	if (vRefNum) *vRefNum = 0;
	if (freeBytes == NULL)
		return noErr;
	*freeBytes = 0;
	if (statvfs(".", &sv) < 0)
		return unix_err();
	*freeBytes = (long)(sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize));
	return noErr;
}

OSErr SetVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }
OSErr FlushVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }

/* --- directory enumeration (jt315 design picker, roster scan, save glob) ---
 *
 * The GEMDOS backend runs Fsfirst/Fsnext over a DTA; here it is an opendir
 * walk with a private GEMDOS-dialect matcher:
 *   - '*' and '?' globs, matched CASE-INSENSITIVELY (Fsfirst is; and the
 *     engine's globs like "SavGam*.csv" count on finding "SAVGAMA.CSV");
 *   - attr 0x10 (FA_SUBDIR) = directories ALSO match; attr 0 = files only;
 *   - the pattern may carry a directory prefix ('\' or '/'), e.g.
 *     "HEIRS.DSN\SavGam*.csv" — the scan runs inside that directory.
 * One scan at a time, exactly like the single-DTA GEMDOS twin. */

static DIR  *s_scan_dir;
static char  s_scan_path[256];		/* the directory being scanned */
static char  s_scan_pat[64];
static int   s_scan_attr;
static int   s_scan_isdir;

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
	if (s_scan_dir != NULL) {
		closedir(s_scan_dir);
		s_scan_dir = NULL;
	}
}

/* Advance until an entry passes the glob + attr filter; copy its name out.
 * Returns 1 on a match, 0 (and closes the scan) when exhausted. */
static int scan_next_match(char *out, int max)
{
	struct dirent *e;

	while (s_scan_dir != NULL && (e = readdir(s_scan_dir)) != NULL) {
		char full[512];
		struct stat st;
		int is_dir, j;

		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (!glob_match(s_scan_pat, e->d_name))
			continue;
		strcpy(full, s_scan_path);
		strcat(full, "/");
		strncat(full, e->d_name, sizeof full - strlen(full) - 1);
		is_dir = stat(full, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
		if (is_dir && !(s_scan_attr & 0x10))
			continue;               /* dirs only match when asked for */
		for (j = 0; j < max - 1 && e->d_name[j]; j++)
			out[j] = e->d_name[j];
		out[j] = '\0';
		s_scan_isdir = is_dir;
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
		strcpy(dir, ".");
		name = pattern;
	}
	ci_resolve(dir, s_scan_path, sizeof s_scan_path);
	for (i = 0; name[i] != '\0' && i < (int)sizeof s_scan_pat - 1; i++)
		s_scan_pat[i] = name[i];
	s_scan_pat[i] = '\0';
	s_scan_attr = attr;
	s_scan_isdir = 0;
	s_scan_dir = opendir(s_scan_path);
	if (s_scan_dir == NULL)
		return 0;
	return scan_next_match(out, max);
}

int files_find_first(const char *pattern, char *out, int max)
{
	return files_find_first_attr(pattern, 0, out, max);  /* attr 0 = files */
}

/* Whether the entry the last find_first/find_next landed on is a directory. */
int files_find_is_dir(void)
{
	return s_scan_isdir;
}

int files_find_next(char *out, int max)
{
	if (out == NULL || max < 1 || s_scan_dir == NULL)
		return 0;
	return scan_next_match(out, max);
}

#endif /* FRUA_UNIX */
