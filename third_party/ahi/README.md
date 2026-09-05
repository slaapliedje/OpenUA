# AHI device header (third party)

`include/devices/ahi.h` is taken verbatim from the **AHI developer archive**,
`dev/misc/ahidev_4.18.lha` on Aminet (file dated 1997-04-27, `$VER: ahi.h 4.2`),
with CRLF line endings converted to LF. Nothing else is changed.

AHI is (C) Copyright 1994-1997 Martin Blom. The developer archive is
distributed for exactly this purpose — building software that talks to
`ahi.device`. Only this one header is vendored: the Amiga backend uses the
**device** API (`OpenDevice("ahi.device")` + `CMD_WRITE` on an `AHIRequest`),
which needs no link library, no library base and no inlines, so the rest of the
archive is not required.

The Bebbo m68k-amigaos toolchain does not ship AHI headers, which is why this
one is vendored rather than picked up from the NDK.

Used by `platform/amiga/sound_paula.c` (the AHI output path; see the "AHI
BACKEND" comment there).
