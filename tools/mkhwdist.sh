#!/usr/bin/env bash
#
# mkhwdist.sh — turn the release zips into media a REAL MACHINE can read.
#
#   tools/mkhwdist.sh <version> [distdir]
#
# The release zips are for people with a PC in the loop. These are for the
# machines themselves: FAT12 .ST images for the Atari side and FFS .ADF images
# for the Amiga side, both of which a Gotek/HxC serves directly and both of
# which write to real floppies.
#
# ★ CAPACITY IS THE WHOLE DESIGN, so the numbers are here rather than in a
#   comment somewhere else:
#
#     Atari DD (Mega ST)      737,280   binary is 1,063,312 — DOES NOT FIT
#     Atari HD (Falcon)     1,474,560   binary is 1,047,134 — fits raw
#     Amiga DD                901,120   AGA 691,092 fits raw; ECS 993,296 does not
#
#   So the Falcon and AGA disks carry the binary as-is and are ready to run,
#   while the Mega ST and ECS disks carry a compressed payload. That asymmetry
#   is not tidiness — it is the media.
#
#   The Mega ST case has no clean floppy answer at all: 1,063,312 exceeds even
#   the extended ST formats a Gotek can serve (82x11x2 = 923,648), and the
#   WD1772 cannot read HD media, so the Mega STE trick does not apply either.
#   Its disk therefore carries FRUA.ZIP and expects mass storage at the other
#   end — which that machine needs regardless, because the game data alone is
#   ~7.4 MB (see HARDWARE.md).
#
set -euo pipefail

VERSION="${1:?usage: mkhwdist.sh <version> [distdir]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DIST="${2:-$REPO/dist}"
OUT="$DIST/hw"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

XDFTOOL="$REPO/tools/.venv/bin/xdftool"
say() { echo "hwdist: $*"; }

for t in mformat mcopy mdir zip split; do
	command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; exit 1; }
done
[[ -x "$XDFTOOL" ]] || { echo "missing $XDFTOOL (pip install amitools)" >&2; exit 1; }

mkdir -p "$OUT"
for z in "$DIST"/openua-*-"$VERSION".zip; do
	[[ -f "$z" ]] || { echo "no release zips for $VERSION in $DIST" >&2; exit 1; }
	unzip -q "$z" -d "$WORK"
done

FAL="$WORK/openua-falcon-$VERSION"
ST="$WORK/openua-atari-st-$VERSION"
AGA="$WORK/openua-amiga-$VERSION"
ECS="$WORK/openua-amiga-ecs-$VERSION"
for d in "$FAL" "$ST" "$AGA" "$ECS"; do
	[[ -d "$d" ]] || { echo "expected unpacked dir missing: $d" >&2; exit 1; }
done

# --- the on-disk read-me, per machine -------------------------------------
# Deliberately short and machine-specific: the long story is HARDWARE.md on
# the PC, and nobody reads a 15 K README off a floppy.
readme() {          # readme <file> <machine> <body...>
	local f="$1" machine="$2"; shift 2
	{
		echo "OpenUA $VERSION — $machine"
		echo
		printf '%s\n' "$@"
		echo
		echo "NO GAME DATA IS INCLUDED. You supply your own from a legally"
		echo "obtained copy of Unlimited Adventures; see GAMEDATA.md in the"
		echo "release zip. A minimum playable install is about 7.4 MB, so it"
		echo "needs a hard disk, CF or SD — not floppies."
		echo
		echo "This build has never run on real hardware. Whatever happens,"
		echo "that is worth reporting."
	} > "$f"
}

# --- Atari: FAT12, no partition table (TOS reads MS-DOS floppies) ---------
atari_img() {       # atari_img <out.st> <size-kb> <srcdir> <file...>
	local img="$1" kb="$2" src="$3"; shift 3
	rm -f "$img"
	mformat -C -f "$kb" -v OPENUA -i "$img" ::
	for f in "$@"; do mcopy -i "$img" "$src/$f" ::/ ; done
	local free
	free=$(mdir -i "$img" :: | grep -F 'bytes free' | tr -s ' ')
	say "$(basename "$img")  ${kb}K  —$free"
}

say "Falcon030 / TT030 — 1.44 MB, runs straight off the disk"
cp "$FAL/frua.prg" "$WORK/FRUA.PRG"
cp "$FAL/UAINST.TTP" "$WORK/UAINST.TTP"
readme "$WORK/README.TXT" "Atari Falcon030 / TT030" \
	"FRUA.PRG    the engine — double-click it." \
	"UAINST.TTP  installs a DOS fan module from its ZIP." \
	"" \
	"Copy both to your hard disk / CF and run from there; the game" \
	"writes saves next to its data."
atari_img "$OUT/openua-falcon-$VERSION.st" 1440 "$WORK" \
	FRUA.PRG UAINST.TTP README.TXT
rm -f "$WORK/FRUA.PRG"

say "Mega ST / STE — 720 KB, compressed (the binary is 1.04 MB)"
( cd "$ST" && zip -q -9 "$WORK/FRUA.ZIP" frua.prg )
readme "$WORK/README.TXT" "Atari ST / STE / Mega ST" \
	"FRUA.ZIP    the engine, zipped — 1,063,312 bytes unpacked, which is" \
	"            more than this 720K disk holds. Unpack it onto your hard" \
	"            disk / CF (PC-side is easiest) and run FRUA.PRG there." \
	"UAINST.TTP  installs a DOS fan module from its ZIP." \
	"" \
	"Needs 2 MB and a colour monitor. ST-low, 16 colours."
atari_img "$OUT/openua-atari-st-$VERSION.st" 720 "$WORK" \
	FRUA.ZIP UAINST.TTP README.TXT
rm -f "$WORK/FRUA.ZIP"

# --- Amiga: FFS ADF -------------------------------------------------------
amiga_img() {       # amiga_img <out.adf> <label> <srcdir> <file...>
	local img="$1" label="$2" src="$3"; shift 3
	rm -f "$img"
	"$XDFTOOL" "$img" create + format "$label" ffs >/dev/null
	for f in "$@"; do "$XDFTOOL" "$img" write "$src/$f" >/dev/null; done
	# `free` is NOT an xdftool command — it exits 2, which under set -e kills
	# the whole run after the image is already written. `info` is the one.
	local free
	free=$("$XDFTOOL" "$img" info | awk '/^free:/ {print $3" free"}')
	say "$(basename "$img")  880K  — $free"
}

say "Amiga AGA (A1200/A4000) — 880 KB, binary raw"
readme "$WORK/README" "Amiga AGA (A1200 / A4000)" \
	"frua    the engine. Needs Kickstart 3.0+ and about 4 MB." \
	"uainst  installs a DOS fan module from its ZIP." \
	"" \
	"Copy both to your hard disk / CF and run from there."
amiga_img "$OUT/openua-amiga-aga-$VERSION.adf" "OpenUA-AGA" "$AGA" \
	frua uainst uainst.info
"$XDFTOOL" "$OUT/openua-amiga-aga-$VERSION.adf" write "$WORK/README" >/dev/null

say "Amiga ECS/OCS (A500+/A600/A2000) — 993 KB binary, so a TWO-disk set"
# ★ NO ARCHIVER. The obvious answer is LhA, but this host's `lha` is lhasa
# (extract-only) and 7z cannot create .lzh either — and more to the point,
# requiring LhA on the target is a dependency the user might not have. AmigaDOS
# has shipped `Join` in C: since 1.2, so splitting in half and joining on the
# machine needs nothing but the stock OS. Halves land ~485 K each, comfortably
# inside an 880 K disk alongside the installer.
split -n 2 -d "$ECS/frua" "$WORK/frua."
readme "$WORK/README" "Amiga ECS / OCS — disk 1 of 2" \
	"The engine is 993,296 bytes and an 880K disk holds ~878K, so it is" \
	"split in half. Copy BOTH halves to your hard disk / CF, then use the" \
	"stock AmigaDOS Join command — no LhA or unzip needed:" \
	"" \
	"    Copy DF0:frua.00 TO DH0:            (this disk)" \
	"    Copy DF0:frua.01 TO DH0:            (disk 2)" \
	"    Join DH0:frua.00 DH0:frua.01 AS DH0:frua" \
	"    Protect DH0:frua +e" \
	"    Delete DH0:frua.00 DH0:frua.01" \
	"" \
	"uainst installs a DOS fan module from its ZIP." \
	"" \
	"Needs Kickstart 2.0+ and 2 MB. Kickstart 1.3 will NOT work — it dies" \
	"in the C startup before the program begins. Native 32-colour."
amiga_img "$OUT/openua-amiga-ecs-$VERSION-disk1.adf" "OpenUA-ECS-1" "$WORK" \
	frua.00 README
"$XDFTOOL" "$OUT/openua-amiga-ecs-$VERSION-disk1.adf" write "$ECS/uainst" >/dev/null
"$XDFTOOL" "$OUT/openua-amiga-ecs-$VERSION-disk1.adf" write "$ECS/uainst.info" >/dev/null

readme "$WORK/README" "Amiga ECS / OCS — disk 2 of 2" \
	"frua.01 is the second half of the engine. See disk 1's README for the" \
	"three-line Join recipe."
amiga_img "$OUT/openua-amiga-ecs-$VERSION-disk2.adf" "OpenUA-ECS-2" "$WORK" \
	frua.01 README

say "done:"
ls -la "$OUT"
