#!/usr/bin/env bash
#
# Stage SpeedoGDOS 5.7 into the Hatari C: mount (data/work/gamedata) from the
# five distribution floppies at data/SPDO57_[1-5].ST. Hand-install — the
# shipped INSTALL.PRG is never run.
#
# Layout produced (all under the GEMDOS C: drive):
#   AUTO\SPEEDO57.PRG + SPDSPD57.PRX + SPDTTF57.PRX   the GDOS + font engines
#   GEMSYS\FX80.SYS META.SYS MEMORY.SYS               printer/metafile drivers
#   FONTS\BX*.SPD + *.TDF                             Bitstream base faces
#   ASSIGN.SYS                                        devices: 1-9 screen (resident),
#                                                     21 = FX80, 31 = META
#   EXTEND.SYS                                        font path + caches; STOP=0
#   SPDTMP\CHR\                                       Speedo's cache dir
#
# Notes learned staging this by hand (2026-07-12):
#   - The shipped ASSIGN.SYS template lives at ASSIGN\NOFONTS\ASSIGN.SYS with a
#     PATHX placeholder; EXTEND.SYS ships as EXTEND\{SMALL,LARGE}\EXTEND.[12]
#     (small/large cache presets) with the same placeholder.
#   - EXTEND.SYS STOP=1 pauses the boot at the Speedo banner with "Press any
#     key" — deadly for the headless harness. Force STOP=0.
#   - CACHEFILE=#ECACHE2# is an installer placeholder; Speedo warns about the
#     '#'. Strip to a plain name.
#   - Speedo 5.7a loads fine from Hatari's GEMDOS-HD AUTO folder and frua.prg
#     boots normally on top of it (verified: banner + font scan + menu).
set -euo pipefail
cd "$(dirname "$0")/.."

G=data/work/gamedata
D=data
for i in 1 2 3 4 5; do
	[[ -f $D/SPDO57_$i.ST ]] || { echo "missing $D/SPDO57_$i.ST" >&2; exit 1; }
done
command -v mcopy >/dev/null || { echo "mtools not installed (pacman -S mtools)" >&2; exit 1; }

mkdir -p $G/AUTO $G/GEMSYS $G/FONTS $G/SPDTMP/CHR

mcopy -o -i $D/SPDO57_1.ST ::AUTO/SPEEDO57.PRG ::AUTO/SPDSPD57.PRX ::AUTO/SPDTTF57.PRX $G/AUTO/
mcopy -o -i $D/SPDO57_2.ST ::DRIVERS/FX80.SYS $G/GEMSYS/
mcopy -o -i $D/SPDO57_5.ST ::DRIVERS/META.SYS ::DRIVERS/MEMORY.SYS $G/GEMSYS/
# The Bitstream base faces (Swiss/Dutch roman+bold+italics) + Monospace 821.
mcopy -o -i $D/SPDO57_3.ST ::FONTS/BX000003.SPD ::FONTS/BX000004.SPD \
      ::FONTS/BX000005.SPD ::FONTS/BX000006.SPD ::FONTS/BX000510.SPD $G/FONTS/
mcopy -o -i $D/SPDO57_5.ST ::FONTS/AA0003.TDF ::FONTS/AB0004.TDF \
      ::FONTS/AC0005.TDF ::FONTS/AD0006.TDF ::FONTS/OO0510.TDF $G/FONTS/

printf 'PATH=C:\\GEMSYS\\\r\n1P SCREEN.SYS\r\n2P SCREEN.SYS\r\n3P SCREEN.SYS\r\n4P SCREEN.SYS\r\n5P SCREEN.SYS\r\n6P SCREEN.SYS\r\n7P SCREEN.SYS\r\n8P SCREEN.SYS\r\n9P SCREEN.SYS\r\n21 FX80.SYS\r\n31 META.SYS\r\n' > $G/ASSIGN.SYS

TMP=$(mktemp)
mcopy -o -i $D/SPDO57_1.ST ::EXTEND/SMALL/EXTEND.1 "$TMP"
sed 's/^PATHX\r*$/PATH=C:\\FONTS\\\r/;
     s/^STOP=1\r*$/STOP=0\r/;
     s/^CACHEFILE=#ECACHE2#\r*$/CACHEFILE=ECACHE2\r/' "$TMP" > $G/EXTEND.SYS
rm -f "$TMP"

echo "SpeedoGDOS 5.7 staged into $G (AUTO + GEMSYS + FONTS + ASSIGN.SYS + EXTEND.SYS)"
