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
 * ★ SCAFFOLD STATUS: the descriptor table and control flow are real; the
 * dos.library calls (Open/Read/Write/Seek/Close/DeleteFile/CreateDir, and the
 * Examine/ExNext directory scan) are marked TODO(hw) and currently return a
 * benign error, so no path is mistaken for a tested implementation. Fill these
 * in and validate on amiberry once the Bebbo toolchain lands.
 */

#ifdef FRUA_AMIGA

#include <stddef.h>
#include <string.h>

#include "amiga_ndk.h"          /* NDK protos, Point-collision-safe wrapper */
#include "files.h"

/* dos.library Seek() modes. */
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

/* Map an IoErr() code to the nearest Mac OSErr. */
static OSErr amiga_err(void)
{
	/* TODO(hw): switch on IoErr(): ERROR_OBJECT_NOT_FOUND -> fnfErr,
	 * ERROR_DISK_FULL -> dskFulErr, ERROR_WRITE_PROTECTED -> wPrErr, etc.
	 * Default ioErr so the engine sees a non-zero return. */
	return ioErr;
}

/* Translate a Mac Pascal filename into a C path for dos.library. Mirrors
 * files.c mac_path_to_c: drop everything up to the last ':' EXCEPT a folder
 * component ending in ".DSN" (a design override folder), and turn that ':' into
 * '/'. TODO(hw): share this with files.c via a small common helper rather than
 * duplicating it — for now the logic is intentionally the same. */
static void mac_path_to_c(ConstStr255Param mac, char *out, int max);   /* TODO(hw) */

/* --- core file ops -------------------------------------------------------- */

OSErr FSOpen(ConstStr255Param fileName, short vRefNum, short *refNum)
{
	char path[256];
	(void)vRefNum;
	mac_path_to_c(fileName, path, sizeof path);
	/* TODO(hw): BPTR b = Open(path, MODE_READWRITE);  if(!b) b=Open(path,MODE_OLDFILE);
	 *           if(!b) return amiga_err();  *refNum = fh_alloc(b); */
	(void)refNum;
	return amiga_err();
}

OSErr FSClose(short refNum)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): Close(b); */
	fh_free(refNum);
	return noErr;
}

OSErr FSRead(short refNum, long *count, void *buffPtr)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): LONG n = Read(b, buffPtr, *count); if(n<0) return amiga_err();
	 *           OSErr e = (n < *count) ? eofErr : noErr; *count = n; return e; */
	(void)buffPtr;
	return amiga_err();
}

OSErr FSWrite(short refNum, long *count, const void *buffPtr)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): LONG n = Write(b, (APTR)buffPtr, *count); if(n<0) return amiga_err();
	 *           *count = n; return noErr; */
	(void)buffPtr;
	return amiga_err();
}

OSErr GetFPos(short refNum, long *filePos)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): *filePos = Seek(b, 0, A_OFFSET_CURRENT); */
	(void)filePos;
	return amiga_err();
}

OSErr SetFPos(short refNum, short posMode, long posOff)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): map Mac posMode (fsFromStart/fsFromLEOF/fsFromMark) to
	 * A_OFFSET_BEGINNING/END/CURRENT and Seek(b, posOff, mode). */
	(void)posMode; (void)posOff;
	return amiga_err();
}

OSErr GetEOF(short refNum, long *logEOF)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): here=Seek(b,0,CURRENT); end=Seek(b,0,END); Seek(b,here,BEGIN);
	 *           *logEOF = end; */
	(void)logEOF;
	return amiga_err();
}

OSErr SetEOF(short refNum, long logEOF)
{
	BPTR b = fh_get(refNum);
	if (!b) return rfNumErr;
	/* TODO(hw): SetFileSize(b, logEOF, OFFSET_BEGINNING). */
	(void)logEOF;
	return amiga_err();
}

OSErr Create(ConstStr255Param fileName, short vRefNum, OSType creator, OSType fileType)
{
	char path[256];
	(void)vRefNum; (void)creator; (void)fileType;
	mac_path_to_c(fileName, path, sizeof path);
	/* TODO(hw): BPTR b = Open(path, MODE_NEWFILE); if(!b) return amiga_err(); Close(b). */
	return amiga_err();
}

OSErr FSDelete(ConstStr255Param fileName, short vRefNum)
{
	char path[256];
	(void)vRefNum;
	mac_path_to_c(fileName, path, sizeof path);
	/* TODO(hw): return DeleteFile(path) ? noErr : amiga_err(); */
	return amiga_err();
}

OSErr DirCreate(short vRefNum, long parentDirID, ConstStr255Param directoryName, long *createdDirID)
{
	char path[256];
	(void)vRefNum; (void)parentDirID; (void)createdDirID;
	mac_path_to_c(directoryName, path, sizeof path);
	/* TODO(hw): BPTR l = CreateDir(path); if(!l) return amiga_err(); UnLock(l). */
	return amiga_err();
}

/* --- Finder info: no dos.library analogue; keep an in-shim cache like the
 * GEMDOS backend does (files.c). Stubbed for the scaffold. --------------- */

OSErr GetFInfo(ConstStr255Param fileName, short vRefNum, FInfo *fndrInfo)
{
	(void)fileName; (void)vRefNum;
	if (fndrInfo) memset(fndrInfo, 0, sizeof *fndrInfo);
	/* TODO: mirror files.c's filename-keyed FInfo cache. */
	return noErr;
}

OSErr SetFInfo(ConstStr255Param fileName, short vRefNum, const FInfo *fndrInfo)
{
	(void)fileName; (void)vRefNum; (void)fndrInfo;
	return noErr;   /* TODO: store into the FInfo cache. */
}

/* --- volume stubs (the engine runs from one working dir) ----------------- */

OSErr GetVol(unsigned char *volName, short *vRefNum)
{
	if (volName) volName[0] = 0;
	if (vRefNum) *vRefNum = 0;
	return noErr;
}
OSErr GetVInfo(ConstStr255Param volName, short *vRefNum, long *freeBytes)
{
	(void)volName;
	if (vRefNum) *vRefNum = 0;
	/* TODO(hw): Info() on the current lock -> free bytes. */
	if (freeBytes) *freeBytes = 0x100000;   /* placeholder 1 MB free */
	return noErr;
}
OSErr SetVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }
OSErr FlushVol(ConstStr255Param volName, short vRefNum) { (void)volName; (void)vRefNum; return noErr; }

/* --- directory enumeration (jt315 design picker, roster scan) ------------
 * The GEMDOS backend uses Fsfirst/Fsnext over a DTA; the Amiga analogue is a
 * Lock + Examine/ExNext (or ExAll). Stubbed empty for the scaffold. */

int files_find_first_attr(const char *pattern, int attr, char *out, int max)
{
	(void)pattern; (void)attr; (void)out; (void)max;
	/* TODO(hw): Lock the parent dir, Examine, ExNext filtering by pattern +
	 * attr (fib_DirEntryType > 0 = directory), copy fib_FileName. */
	return 0;
}
int files_find_first(const char *pattern, char *out, int max)
{
	return files_find_first_attr(pattern, 0, out, max);
}
int files_find_is_dir(void)
{
	return 0;   /* TODO(hw): last ExNext's fib_DirEntryType > 0 */
}
int files_find_next(char *out, int max)
{
	(void)out; (void)max;
	return 0;   /* TODO(hw): ExNext into the FileInfoBlock */
}

#endif /* FRUA_AMIGA */
