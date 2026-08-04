# `VAULT<c>.DAT` — the pending-treasure vault file

Short version: **this format was never unknown.** It is fully lifted as
`jt74` (read) and `jt75` (write) in `src/engine/boot.c`, the port has been
writing the file since long before anyone noticed, and a DOS-written vault
loads without changes. What follows is the spec, so nobody has to re-derive it
from the two functions again.

## Where it lives

`<design>.DSN\SAVE\VAULT<c>.DAT`, beside the slot save it belongs to — same
folder as `SAVGAM<c>.CSV` (see `docs/save-load-wall.md`). The port wrote it to
the flat gamedata folder until 2026-08-03, so a pre-existing install may have
stray `VaultA.DAT` files at the root; they are dead and can be deleted.

`<c>` is `g_a5_-22218`. It is **not always a save slot**: `jt585` sets it to the
slot letter `A`..`J`, while the in-game vault event (`jt185`) sets it to `'T'`,
so `VAULTT.DAT` is the town vault rather than a save. The value `'Z'` (90)
means "no vault" and both `jt583` (load) and `jt586` (save) return immediately
on it.

## Layout

| offset | size | contents |
|---|---|---|
| 0 | 12 | three money longs, **little-endian** (`g_a5_-25314..`) |
| 12 | 2 | `0xFFFF` sentinel |
| 14 | 2 | item count, little-endian |
| 16 | 18 × N | one record per item, in list order |
| … | 18 × pad | unused records, copied from the item-template table (`g_a5_-27920`) |

Each 18-byte record is the item node's template at node offset +40. The two
word fields at node +44 and +46 are byte-swapped on the way out and swapped
back afterwards, so the in-memory node is left untouched by a write.

A record whose first byte is **73** is a *bundle*: its member items follow
immediately, `node[53]` of them, walked through the `+58` chain. Members are
written inline, so the item count in the header counts top-level items only and
the file is longer than `count × 18` when bundles are present. The reader
reverses this exactly.

## The pad is where Mac and DOS differ

| release | pads to | file size with an empty vault |
|---|---|---|
| Macintosh (what we lift) | **200** records | 3616 bytes |
| MS-DOS 1.2 | **50** records | 916 bytes |

Verified both ways: the Mac constant is `cmpiw #200` / `movew #200` / `mulsw
#18` at `CODE 6 + 0x63f0` in the disassembly, and a DOS-written `VAULTF.DAT`
measures 916 = 12 + 4 + 50 × 18.

**It does not matter for reading.** `jt74` takes the item count from the header
and reads exactly that many records; the pad is never touched. That is why a
916-byte DOS vault loads into this port unchanged, and why the port's own
3616-byte file is not a compatibility problem in the direction anyone cares
about. Writing a file a *DOS* build would accept is the untested direction — if
that is ever wanted, the pad target is the only knob.

## Verified

2026-08-03, Falcon, Hatari. A DOS-written pair (`SAVGAMA.CSV` 10 285 bytes +
`VAULTA.DAT` 916 bytes, produced by driving the DOS release headlessly) was
dropped into `HEIRS.DSN\SAVE\` and loaded through PLAY → LOAD SAVED GAME → A:
all six characters came back with their AC and HP (BARBARUS, LADY ILLIS,
MALTIER, NIVLOC, CLARANA, STRANILLA). Saving to slot B then wrote both
`SAVGAMB.CSV` and `VaultB.DAT` into the same folder.

So **DOS saves load as they are.** The one-byte difference in the slot file
(DOS 10 285, ours 10 284) does not obstruct it either — the deserializer is
field-driven, not length-driven.

## Reading the code

- `jt583` (CODE 15 + 0x1c92) — load: builds `Vault%c.DAT`, opens it through
  `l00e0_load`, runs `jt74`.
- `jt586` (CODE 15 + 0x1cd2) — save: same name, `l00e0`, runs `jt75`.
- `jt74` (CODE 6 + 0x6288-ish) — the reader; count-driven, rebuilds the
  `-25302` item list through `jt477`/`jt406`.
- `jt75` (CODE 6 + 0x61da) — the writer; described above.
- `l61c6` (CODE 6 + 0x61c6) — stores the loaded item count into `-13048`.
