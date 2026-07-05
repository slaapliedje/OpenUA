# HFS / GEMDOS file shim — coverage audit (2026-07-04)

Triggered by "build the HFS/GEMDOS shim" (the last-5% door that was
framed as *unblocking the file-trap glue*). The investigation's verdict:

> **The HFS/GEMDOS shim is already built and complete for every live
> port path.** Nothing in the MISSING/STUB list is actually blocked on
> shim-building. The "file" entries still open are either *superseded*
> by the port's ratified GEMDOS-native model or are *not file I/O at
> all* (date, memory-manager, RNG, or Mac low-memory internals).

## What the shim already covers (`compat/files.c`, 483 lines)

| Mac Toolbox | GEMDOS mapping | notes |
|---|---|---|
| `FSOpen` / `FSClose` | `Fopen` / `Fclose` | refNum = GEMDOS handle in the int16 slot |
| `FSRead` / `FSWrite` | `Fread` / `Fwrite` | short read → `eofErr`, count written back |
| `GetEOF` / `SetEOF` | `Fseek` (end) / `Fseek`+truncate | |
| `GetFPos` / `SetFPos` | `Fseek` (cur/set/end) | |
| `Create` / `FSDelete` | `Fcreate` / `Fdelete` | |
| `DirCreate` | `Dcreate` | |
| `GetFInfo` / `SetFInfo` | `Fattrib` + Finder-info shim | |
| `GetVol` / `SetVol` | default-vol sentinel (vRefNum 0) | see "known-incomplete" below |
| `GetVInfo` | `Dfree` (free = b_free·secsiz·clsiz) | free-space query works |
| `FlushVol` | no-op | GEMDOS writes synchronously |
| directory walk | `Fsetdta` + `Fsfirst` / `Fsnext` | `files_find_first[_attr]` / `_next` / `_is_dir` |

Error translation goes through `gemdos_err()` / `gemdos_to_oserr()` at
the shim boundary; callers keep Mac `OSErr`. The async-PB and single-
call trap glue is already routed here and marked done in
`jt_progress.py ALIAS_LIFTED`: **1045** (`_GetVInfo` PB), **1054**
(`_Delete`), **1059** (`FSDispatch` sel 9/10/16/17/11 catalog PB),
**1060** (async-PB volume).

## The "still-open file" entries are NOT shim gaps

| JT | CODE | reality | evidence |
|---|---|---|---|
| **jt426** | 3+0x4da6 | **Superseded.** Mac indexed-catalog OPEN. Its *only* caller is jt990 (CODE 5+0x1b76). | `JT[426]` appears once in the disasm — CODE_05 @0x1c66, inside jt990. |
| **jt432** | 3+0x4f78 | **Superseded.** Mac catalog READ-NEXT (calls jt1059's PB). Its *only* caller is jt991 (CODE 5+0x1cb6). | `JT[432]` appears once — CODE_05 @0x1d5c, inside jt991. |
| **jt458** | 3+0x0846 | **Superseded.** Volume/drive enumeration (48-slot -10074 table). Its *only* caller is **jt12**, the Mac boot mega-initializer. | `JT[458]` appears once — CODE_06 @0x07bc, inside `entry_jt12`. |

`jt990` / `jt991` are **already lifted** (boot.c:25609 / :25629) as the
port's directory walk, and they explicitly **bypass** jt426/jt432 by
going straight to GEMDOS `Fsfirst`/`Fsnext` — this is a *ratified
decision* recorded in their header: "The faithful Atari mapping is a
GEMDOS Fsfirst/Fsnext wildcard scan." `jt12` is the Mac application
startup sequence (loads `:DISK4:ALWAYS.CTL`, chains JT[136/260/489/561/
594/709/859/905/962/1207/234/252/271/326/329]); the port replaces the
whole thing with its own `boot.c` init, so the volume-enumeration path
is never taken.

**Conclusion:** lifting jt426/jt432/jt458 would produce dead code (no
live caller). They stay MISSING *by design* — the port model supersedes
them. Do not lift them; do not fabricate a PBGetCatInfo/Drvmap shim for
them.

## The other "trap glue" entries are not HFS at all

Earlier notes lumped these with the file shim; the disasm says
otherwise:

| JT | CODE | what it actually is | belongs to |
|---|---|---|---|
| **jt1039** | 5+0x5560 | date/time: `_SecondsToDate` / `_DateToSeconds` (+ string cmp) — save-timestamp formatting | a Toolbox **date** shim (GEMDOS `Tgetdate`/`Tgettime`) |
| **jt1063** | 5+0x65c8 | Memory Manager: `_HandToHand` / `_PtrToHand` / `_PtrToXHand` / `_Get/SetHandleSize` / `_BlockMove` — handle duplication | the **macmemory** shim |
| **jt1143** | 4+0x7b8a | 7-line RNG seed: `seed = jt1039() ^ _TickCount()` | RNG (depends on jt1039) |
| **jt1027** | 5+0x4d86 | pokes Mac low-memory (`0x28e`, the `0x114`/`0x2aa` 12-byte free-list) — Mac OS heap/queue internals | **moot on Atari** (NOOP candidate; no such low-mem structs) |

None of these need HFS/GEMDOS work. jt1063 (macmemory) and jt1039→jt1143
(date + RNG) are tractable faithful lifts in *their own* shims;
jt1027 is a Mac-internal no-op.

## Known-incomplete (but unused) shim corner

`GetVol` returns an empty volume name and vRefNum 0 (the Mac "default
volume" sentinel). No live port path reads the *name* (callers use
`FSOpen` on a working-dir path, and `GetVInfo`/`Dfree` for free space),
so this is acceptable as-is. If a future path needs the real current
drive, wire `Dgetdrv` + a synthesized `A:`..`P:` label — but not
speculatively.

## Net

Nothing to build here. The shim is done. This audit reclassifies the
seven "file-ish" open entries so the next session doesn't re-chase a
shim that already exists.
