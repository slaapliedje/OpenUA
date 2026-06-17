# Macintosh Memory Manager (Handles & purgeable blocks)

Authored reference for the relocatable-block / purgeable-handle model the
Resource Manager and FRUA's FC cache depend on. Canonical source: *Inside
Macintosh: Memory*. Scoped to what the lifts touch. See also `resource-manager`
and `fc-group-cache`.

## Pointers vs Handles

- **`Ptr`** = `char *` — a **non-relocatable** block (`NewPtr(size)`). The
  Memory Manager will not move it; it can fragment the heap. Use for blocks the
  hardware touches at a fixed address (DMA/video) — on Atari, allocate ST-RAM
  via `Mxalloc(size, 0)` and flag as `NewPtrST` (CLAUDE.md memory rules).
- **`Handle`** = `Ptr *` = `**` — a **relocatable** block. You hold a *handle*
  (pointer to a *master pointer*); the Memory Manager may move the block and
  update the master pointer, so `*h` (the master pointer) is only valid until
  the next heap-moving call. `NewHandle(size)`.

```
Handle h = NewHandle(1024);   // h is stable; *h may change
HLock(h);                     // pin the block; *h now stable
Ptr p = *h;                   // safe to keep p while locked
... use p ...
HUnlock(h);                   // unpin; *h may move again
```

## Why relocatable + purgeable

The classic Mac heap is small and fragments. Relocatable blocks let the Memory
Manager **compact** (slide blocks together) to satisfy a large request.
**Purgeable** blocks go further: under memory pressure the Manager can *throw
the data away* (set the master pointer to NULL) and reclaim the space, on the
promise that the owner can regenerate it. The Resource Manager uses this so
rarely-touched resources cost nothing when idle.

## The locking / purge state machine

A handle's block is in one of: **unlocked** (movable), **locked** (pinned), and
independently **purgeable** or not, and **purged** (data gone, master ptr NULL).

- `HLock(h)` / `HUnlock(h)` — pin / unpin. A locked block is never moved or
  purged. **Lock before dereferencing across any call that allocates.**
- `HPurge(h)` / `HNoPurge(h)` — mark the block purgeable / not. A purgeable,
  unlocked block may vanish on the next allocation.
- `EmptyHandle(h)` — purge now (free the data, keep the handle, master ptr →
  NULL). The handle is reusable via `ReallocateHandle`.
- `ReallocateHandle(h, size)` — re-acquire space for a previously emptied/purged
  handle (does NOT restore contents — the RM's `LoadResource` refills resource
  handles).
- `DisposeHandle(h)` — free the handle entirely (handle invalid afterward).

### The cardinal rule

> After ANY call that can allocate or move memory, a handle you hold may have
> moved (master pointer changed) or been **purged** (master pointer NULL).
> Re-fetch `*h` after locking, and for resources call `LoadResource(h)` /
> re-`GetResource` before use.

A dereference of a purged handle reads through a NULL master pointer → a bus
error at a low address. (FRUA's port surfaces these as the classic "Bus Error
reading at $0" — the same failure mode a dangling roster pointer produces.)

## Sizing & inspection

- `GetHandleSize(h) -> Size` / `SetHandleSize(h, newSize)` — query/resize
  (resize may move the block; resize can fail with `memFullErr`).
- `GetPtrSize` / `SetPtrSize` — same for pointers.
- `RecoverHandle(Ptr) -> Handle` — given a master pointer, get its handle.
- `HGetState(h) -> char` / `HSetState(h, state)` — save/restore the lock+purge
  flags around a critical section (cleaner than paired HLock/HUnlock when state
  may already be set):

```
char st = HGetState(h);
HLock(h);
... use *h ...
HSetState(h, st);             // restore exactly what it was
```

## Errors

`MemError() -> OSErr`: `noErr`, `memFullErr = -108` (out of memory),
`nilHandleErr = -109` (dereferenced an empty/NULL handle), `memWZErr = -111`
(operation on a free block). Check after `NewHandle`/`SetHandleSize`.

## Atari port mapping (compat / platform rules)

- `NewPtr`/`NewHandle` map over the Atari allocator (`Mxalloc`/malloc); keep Mac
  spellings and `OSErr` returns. `DisposeHandle`/`DisposePtr` free.
- Buffers the VIDEL / DMA-sound / DSP read must be **ST-RAM** — `Mxalloc(size,0)`,
  flagged `NewPtrST`, called from `platform/` not engine code (CLAUDE.md).
- The port does not implement a moving/compacting heap, so "Handles never move"
  is true in practice — but DO preserve the **purgeable** semantics where the
  engine relies on reload-on-demand (the FC cache, big art libraries), so the
  4 MB footprint stays bounded (see the port-memory-vs-Mac-1MB note: loading
  whole files resident is why the port needs >RAM than the Mac's purgeable RM).
