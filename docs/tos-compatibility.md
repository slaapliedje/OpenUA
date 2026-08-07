# Which TOS versions the Atari builds run on

Measured 2026-08-06/07 in Hatari 2.6.1, prompted by a field report from a
STacy owner. Until then every ST/STe boot this project had ever done used
TOS 2.06, because the harness picks the ROM from `--machine` and only the
`ste` arm had a caller. That single blind spot hid a bug that stopped the
engine dead on most real ST and STe machines.

## The GEMDOS version is the thing that matters

Atari's TOS releases carry a separate GEMDOS revision, and it is the GEMDOS
number — not the TOS number — that decides which calls exist. From the
Compendium's `Sversion()` table:

| GEMDOS | TOS releases |
|---|---|
| 0.13 | 1.00, 1.02 |
| 0.15 | 1.04, 1.06 |
| 0.17 | 1.62 |
| 0.19 | 2.01, 2.05, 2.06, 3.01, 3.05, 3.06 |
| 0.30 | 4.00 … 4.04 |

`Mxalloc` (GEMDOS 0x44) and `Maddalt` (0x14) are both **0.19 and later**. So
on any ST-era or STe-era ROM there is no `Mxalloc` — and, just as importantly,
no alternative RAM either, which is what makes plain `Malloc` a complete
substitute there rather than a compromise. See the note in
`platform/sys_falcon.c`.

## Verified boots

Each row is an observed run reaching the engine's own `menu: modal up` marker,
at 4 MB, with the GEMDOS mount and no floppy.

| machine | TOS | GEMDOS | result |
|---|---|---|---|
| plain ST | 1.04 | 0.15 | menu; `ste: blitter = 0` |
| plain ST | 2.06 | 0.19 | menu; `ste: blitter = 0` |
| STe | 1.62 | 0.17 | menu; `ste: blitter = 1` |
| STe | 2.06 | 0.19 | menu — the long-standing default test target |
| Falcon | 4.04 | 0.30 | menu |

**TOS 1.00 and 1.02 (GEMDOS 0.13) are NOT verified** — no ROM image here. They
take the same `Malloc` path as 1.04 and should behave the same, but nobody has
watched one boot, so do not claim them.

Two ROM facts worth keeping, both of which cost a run to learn:

- **TOS 1.62 is an STE ROM.** Hatari refuses it on `--machine st` and switches
  the machine to STE underneath you ("TOS versions 1.06 and 1.62 are for Atari
  STE only"), so a run that *looks* like a plain-ST test silently is not one.
  A STacy is an ST and therefore cannot be on 1.62; 1.04 or a fitted 2.06.
- **TOS 1.04 supports GEMDOS hard-disk emulation.** This was doubted, and the
  control settles it: 1.04 on an emulated ST with our mount and no `frua.prg`
  boots to the GEM desktop showing a HARD DISK icon, zero bus errors.

## The bug this found

Pre-fix, `Mxalloc` was called unconditionally and only `== NULL` was checked.
An unimplemented GEMDOS opcode returns EINVFN (-32), which is not NULL, so the
guard passed and `st_init` computed a page base of 0 and memset over low
memory. Two different deaths from one cause:

- TOS 1.04 — double bus error, CPU halted, immediately after the backend logs
  its name.
- TOS 1.62 — no bus error; low memory is destroyed and the program unwinds
  ("Closing … file handle(s) remaining at program 0x0 exit").

Both were reproduced deliberately after the fix landed, by rebuilding with the
old logic restored, so the claim rests on observation rather than on reading.

## Still open on a plain ST

`main: sound init failed (continuing silent)` on TOS 1.04/plain ST. That is
correct behaviour for the hardware, not a regression: `sound_falcon.c` drives
the STe/Falcon **DMA** sound, which a plain ST does not have. An ST would need
a YM2149 path. The engine continues silently, so this costs audio, not the
run — but it means the `st` capture has no sound while the `ste` one does.

## Harness notes

- `tools/capture_demo.sh st` exists now (it did not before) and forces TOS
  2.06 via `EXTRA_HATARI`, because the capture predates the fix; revisit that
  once a 1.04 capture is wanted.
- `FRUA_MEM` defaults to **14** MB, which no real ST or STe can have (4 MB is
  the ceiling). It is harmless for most work but is not a shippable
  configuration — pass `FRUA_MEM=4` for anything claiming to represent real
  hardware. Both the 14 MB and 4 MB runs reproduced the bus error identically,
  which is how memory size was ruled out as the cause.
