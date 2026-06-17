# Macintosh Resource Manager

Authored API reference (semantics, not Apple's verbatim text) for the classic
Mac OS Resource Manager, scoped to what FRUA's lifts actually exercise. The
canonical source is *Inside Macintosh: More Macintosh Toolbox*, ch. "Resource
Manager", and *Inside Macintosh: Memory* for the purgeable-handle behaviour the
RM relies on. Pair this with `resource-fork-format`, `memory-manager`, and
`fc-group-cache`.

## Mental model

A *resource* is a typed, numbered blob: identified by a four-byte **ResType**
(e.g. `'CODE'`, `'PICT'`, `'STR#'`) and a signed 16-bit **ID**. Resources live
in the **resource fork** of a file (see `resource-fork-format`). The Resource
Manager keeps a **resource map** in memory per open fork and hands the app
**Handles** (`** to the data`) — relocatable, often **purgeable**, blocks owned
by the Memory Manager (see `memory-manager`).

Key consequence for a port: a `Handle` returned by `GetResource` can become
**purged** (its master pointer set to NULL) at any allocation if marked
purgeable. You must re-`LoadResource` (or re-`GetResource`) before dereferencing
after any call that can move/purge memory. FRUA's FC group cache is built on
exactly this purgeable-handle behaviour (`fc-group-cache`).

## The search chain

Open forks form a **search chain** (most-recently-opened first). `GetResource`
walks it until it finds a (type,id) match. `UseResFile(refNum)` makes one fork
current (front of the chain for *creation* and `Get1*` scoping). System
resources sit at the end of the chain, so an app resource of the same (type,id)
**overrides** the system one — the basis for patching.

- `CurResFile() -> short` — refNum of the current resource file.
- `UseResFile(short refNum)` — set the current resource file.
- `HomeResFile(Handle) -> short` — which fork a resource came from.
- `Get1Resource` / `Get1IndResource` / `Count1Resources` — like the non-`1`
  forms but scoped to the **current** fork only (no chain walk).

## Opening / closing forks

- `OpenResFile(ConstStr255Param name) -> short refNum` — open a file's resource
  fork, push it on the chain. `-1` on failure (`ResError` for detail).
- `OpenRFPerm(name, vRefNum, permission) -> short` — with explicit volume +
  read/write permission (`fsRdPerm`, `fsRdWrPerm`).
- `CloseResFile(short refNum)` — write back any changed resources (if opened
  writable and dirty) and release the map + all its handles.
- `CreateResFile(name)` / `FSpCreateResFile(...)` — make an empty resource fork.

Port note: the Mac Toolbox shim (`compat/`) maps these onto GEMDOS file I/O
against a flat `(type,id)` archive built by `tools/rsrcpack` (ADR-0007); engine
code keeps the Mac spellings and `OSErr` returns (`gemdos_to_oserr`).

## Loading resources

- `GetResource(ResType, short id) -> Handle` — find (type,id) on the chain,
  ensure the data is loaded, return its handle. NULL if not found. **The
  workhorse.**
- `Get1Resource(ResType, id) -> Handle` — current fork only.
- `GetNamedResource(ResType, ConstStr255Param name) -> Handle` — by name.
- `GetIndResource(ResType, short index) -> Handle` — the `index`-th resource of
  a type (1-based) across the chain; `Count Resources` first.
- `LoadResource(Handle)` — (re)read the data for a handle whose memory was
  purged. **No-op if already loaded.** After any heap-moving call, do
  `LoadResource(h)` before using `*h`. Sets `ResError`.
- `ReleaseResource(Handle)` — free the handle's memory and forget it; the map
  entry stays, so a later `GetResource` reloads. Use when done with a resource
  but the fork stays open.
- `DetachResource(Handle)` — sever a handle from the map so it survives
  `CloseResFile` and is owned by the app (you then `DisposeHandle` it).

### The purge / reload discipline (critical)

```
Handle h = GetResource('PICT', 128);   // may be purgeable
HLock(h);                              // pin while dereferencing
... use *h ...
HUnlock(h);                            // allow purge again
// later, after allocations:
LoadResource(h);                       // re-read if it was purged
```

`GetResource` itself reloads if needed, so calling it again is also safe.
FRUA's art loaders lean on this: a GLIB group handle is fetched, locked for the
blit, then unlocked so the FAR pool can purge the least-recently-used group.

## Resource attributes (the attrs byte)

Each map entry carries an 8-bit attribute mask (`GetResAttrs`/`SetResAttrs`):

| bit | const            | meaning                                            |
|-----|------------------|----------------------------------------------------|
| 0   | `resChanged` 0x02| data modified, write back on `UpdateResFile`/close |
| 1   | `resPreload` 0x04| load when the fork is opened                       |
| 2   | `resProtected`0x08| cannot be modified/removed via the RM             |
| 5   | `resLocked` 0x10 | handle is locked (non-relocatable) when loaded     |
| 6   | `resPurgeable`0x20| handle may be made purgeable when loaded          |
| 7   | `resSysHeap` 0x40| load into the system heap                          |

(The on-disk attribute byte is the same mask; `tools/macrsrc.py` exposes it as
`Resource.attrs`.) `resPurgeable` + `resLocked` interact: a locked handle is
never purged while locked. The RM applies these when it loads the data.

## Writing resources (editor paths)

- `AddResource(Handle, ResType, id, Str255 name)` — register a new resource in
  the current fork (handle becomes resource-owned).
- `RemoveResource(Handle)` — drop a resource from the map.
- `ChangedResource(Handle)` — mark dirty (`resChanged`) so it's written back.
- `WriteResource(Handle)` — write one changed resource now.
- `UpdateResFile(short refNum)` / `CloseResFile` — flush all changes.
- `SetResInfo` / `GetResInfo` — read/modify a resource's id+name.

FRUA's runtime is read-mostly (ADR-0008 ports the play runtime first); the
editor write paths are lower priority.

## Errors

`ResError() -> OSErr` returns the error from the most recent RM call (`noErr`,
`resNotFound = -192`, `resFNotFound = -193`, `addResFailed = -194`,
`rmvResFailed = -196`, plus Memory/File Manager errors propagated through).
Always check `ResError` after `GetResource` returns NULL to distinguish
"not found" from "out of memory".

## What FRUA actually uses

- The 23 `'CODE'` segments are the decompilation target (THINK C; A5-relative).
- Art is shipped as `GLIB` containers inside `.CTL`/`.TLB`/`.GLB` files loaded
  through the **FC file cache** (`fc-group-cache`), not always as classic
  resources — but the same purgeable-handle discipline applies.
- String tables (`STR#`, `STRS`), `FONT`/`NFNT`, palettes (`clut`/`pltt`) are
  classic resources the shim serves from the flat archive.

See also: `jt_lookup` for the JT entries that wrap these (e.g. the RM/FC
loaders), and `sym_search` for the `compat/resources.c` shim.
