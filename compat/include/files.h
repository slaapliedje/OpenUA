/*
 * Mac File Manager shim — high-level data-fork I/O (ADR-0003).
 *
 * The engine calls Mac's FSOpen / FSRead / FSWrite / FSClose etc.; this
 * stands those calls up over GEMDOS file operations on the Atari. Mac
 * Pascal filenames translate to GEMDOS C strings (the part after the
 * last ':' — the rest is HFS volume / folder syntax that GEMDOS doesn't
 * speak), vRefNum is ignored (the engine runs out of one working
 * directory), and the GEMDOS file handle doubles as the Mac refNum.
 *
 * Here so far: FSOpen, FSClose, FSRead, FSWrite, GetEOF, SetEOF,
 * GetFPos, SetFPos, Create, FSDelete, GetVol, SetVol, FlushVol,
 * GetFInfo, SetFInfo, and the standard OSErr names. OpenRF / the
 * resource-fork API stays in resources.c — it's served by the flat
 * (type, id) archive (ADR-0007), not by HFS forks.
 */

#ifndef COMPAT_FILES_H
#define COMPAT_FILES_H

#include "quickdraw.h"          /* ConstStr255Param, Point */
#include "macmemory.h"          /* OSErr, Size              */

/* OSErr names — the File Manager subset the engine touches. */
#define noErr        0
#define ioErr      (-36)        /* generic I/O failure                */
#define eofErr     (-39)        /* end of file during read            */
#define posErr     (-40)        /* seek past EOF                      */
#define tmfoErr    (-42)        /* too many files open                */
#define fnfErr     (-43)        /* file not found                     */
#define wPrErr     (-44)        /* write-protected                    */
#define fLckdErr   (-45)        /* file locked                        */
#define dupFNErr   (-48)        /* duplicate filename                 */
#define paramErr   (-50)        /* bad parameter                      */

/* SetFPos posMode */
#define fsAtMark      0         /* offset ignored; seek to mark       */
#define fsFromStart   1         /* offset relative to start of file   */
#define fsFromLEOF    2         /* offset relative to end of file     */
#define fsFromMark    3         /* offset relative to current mark    */

/*
 * 4-byte FourCC. The Mac Finder uses these for file type / creator.
 * They're written byte-aligned big-endian — same wire format as the
 * 'TYPE' literal in the resource shim.
 */
typedef unsigned long OSType;

/* Finder info — the metadata the Mac stores per file. We hold these so
 * GetFInfo / SetFInfo can round-trip; the engine sets type / creator for
 * files it produces but the values don't drive shim behaviour. */
typedef struct FInfo {
	OSType         fdType;
	OSType         fdCreator;
	unsigned short fdFlags;
	Point          fdLocation;
	short          fdFldr;
} FInfo;

OSErr FSOpen(ConstStr255Param fileName, short vRefNum, short *refNum);
OSErr FSClose(short refNum);
OSErr FSRead(short refNum, long *count, void *buffPtr);
OSErr FSWrite(short refNum, long *count, const void *buffPtr);

OSErr GetEOF(short refNum, long *logEOF);
OSErr SetEOF(short refNum, long logEOF);
OSErr GetFPos(short refNum, long *filePos);
OSErr SetFPos(short refNum, short posMode, long posOff);

OSErr Create(ConstStr255Param fileName, short vRefNum,
             OSType creator, OSType fileType);
OSErr FSDelete(ConstStr255Param fileName, short vRefNum);
OSErr DirCreate(short vRefNum, long parentDirID,
                ConstStr255Param dirName, long *createdDirID);

OSErr GetFInfo(ConstStr255Param fileName, short vRefNum, FInfo *fndrInfo);
OSErr SetFInfo(ConstStr255Param fileName, short vRefNum,
               const FInfo *fndrInfo);

OSErr GetVol(unsigned char *volName, short *vRefNum);
OSErr SetVol(ConstStr255Param volName, short vRefNum);
OSErr FlushVol(ConstStr255Param volName, short vRefNum);

/* Directory enumeration (GEMDOS Fsfirst/Fsnext) — list files matching an
 * 8.3 wildcard `pattern` (e.g. "CHAR*.CHR"). first starts a scan, next
 * continues it; each writes the matched filename (C string) to `out`.
 * Returns 1 while a name was found, 0 at the end. One scan at a time. */
int files_find_first(const char *pattern, char *out, int max);
int files_find_first_attr(const char *pattern, int attr, char *out, int max);
int files_find_next(char *out, int max);
int files_find_is_dir(void);   /* 1 if the current match is a directory */

#endif /* COMPAT_FILES_H */
