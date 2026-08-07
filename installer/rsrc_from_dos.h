/*
 * Build frua.rsc from the user's own DOS CKIT.EXE on the target machine.
 * See installer/rsrc_from_dos.c; the PC twin is tools/rsrc_from_dos.py.
 */
#ifndef UAINST_RSRC_FROM_DOS_H
#define UAINST_RSRC_FROM_DOS_H

#define UAINST_RSRC_OK          0
#define UAINST_RSRC_ERR         1
#define UAINST_RSRC_WRONGBUILD  2       /* not the v1.2 CKIT.EXE the map fits */

/* Writes `out_path` (an FRSC archive holding STRS 0) from `exe_path`.
 * `msg` always receives a human-readable result, success or failure.
 * Returns one of the UAINST_RSRC_* codes. */
int uainst_rsrc_from_dos(const char *exe_path, const char *out_path,
                         char *msg, unsigned long msgcap);

/* FIPS known-answer check on the bundled SHA-256. Exposed so the host test
 * can assert the primitive independently of the archive it guards. */
int uainst_sha256_selftest(void);

#endif /* UAINST_RSRC_FROM_DOS_H */
