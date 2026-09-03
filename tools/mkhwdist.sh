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
#   The Mega ST case has no clean answer on a REAL FLOPPY DRIVE: 1,063,312
#   exceeds even the extended formats a WD1772 can be talked into (82x11x2 =
#   923,648), and it cannot read HD media at all.
#
# ★ ON A GOTEK IT DOES FIT, and that changes the ST story. FlashFloppy never
#   raises the data rate — the ST stays at its fixed 250 kbit/s — it SLOWS THE
#   EMULATED ROTATION so more sectors pass the head per turn, and it will serve
#   up to 255 cylinders. 80 x 2 x 18 at 150 rpm is 1,474,560 bytes a stock ST
#   reads as an ordinary DD disk, at half the random-access speed. So this
#   script also emits `openua-st-gotek-<ver>.st`, carrying FRUA.PRG RAW with no
#   PC-side unzip step. It is a Gotek/HxC image, NOT a real floppy — the 720K
#   disk above stays for anyone with an actual drive.
#
#   Geometry and rpm are from phjanderson/flashfloppy-atari-disks; the matching
#   IMG.CFG stanzas are written next to the images. See HARDWARE.md.
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
# ENGINE.LST — instdisk's manifest for the engine step (same header as
# DISK.LST, then "<src> <dest> [a]"). Raw-PRG disks only; the 720K disk
# carries FRUA.ZIP and its README says "unzip on a PC".
printf '1 1 OpenUA engine (Falcon/TT)\nFRUA.PRG FRUA.PRG\nUAINST.TTP UAINST.TTP\n' > "$WORK/ENGINE.LST"
cp "$FAL/UAINST.TTP" "$WORK/UAINST.TTP"
readme "$WORK/README.TXT" "Atari Falcon030 / TT030" \
	"FRUA.PRG    the engine — double-click it." \
	"UAINST.TTP  installs a DOS fan module from its ZIP." \
	"" \
	"Copy both to your hard disk / CF and run from there; the game" \
	"writes saves next to its data."
atari_img "$OUT/openua-falcon-$VERSION.st" 1440 "$WORK" \
	FRUA.PRG UAINST.TTP README.TXT ENGINE.LST
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

say "Atari ST on a GOTEK — 1.44 MB at 150 rpm, binary raw"
# Same 80x2x18 geometry a Falcon disk uses; what makes a STOCK ST able to read
# it is FlashFloppy turning the disk at half speed, so the 250 kbit/s the
# WD1772 is locked to still yields 18 sectors per track. Verified in Hatari:
# the ST binary autostarts from a 1.44 MB image on `--machine st`.
cp "$ST/frua.prg" "$WORK/FRUA.PRG"
printf '1 1 OpenUA engine (ST Gotek)\nFRUA.PRG FRUA.PRG\nUAINST.TTP UAINST.TTP\n' > "$WORK/ENGINE.LST"
readme "$WORK/README.TXT" "Atari ST / STE / Mega ST — GOTEK IMAGE" \
	"FRUA.PRG    the engine, ready to copy off — no unzip step." \
	"UAINST.TTP  installs a DOS fan module from its ZIP." \
	"" \
	"THIS IS A GOTEK / HxC IMAGE, NOT A REAL FLOPPY. It needs FlashFloppy" \
	"and the IMG.CFG stanza shipped beside it, because a stock ST cannot" \
	"read 1.44 MB media — FlashFloppy turns the disk at 150 rpm instead of" \
	"300 so the ST's fixed 250 kbit/s still reads 18 sectors per track." \
	"Random access is about half a real floppy's; copying once is fine." \
	"" \
	"With a real drive, use the 720K disk instead."
atari_img "$OUT/openua-st-gotek-$VERSION.st" 1440 "$WORK" \
	FRUA.PRG UAINST.TTP README.TXT ENGINE.LST
rm -f "$WORK/FRUA.PRG"

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

# --- Amiga Workbench install: an IconX launcher that finds whichever
# Installer the machine has and runs Install.script with it. Installer is
# standard since Workbench 2.0 but lives in SYS:Utilities on 2.x/3.0/3.1
# and SYS:System on 3.1.4/3.2 — neither is in the path, and IconX (in C:)
# is. Nothing is shipped that is not ours.
amiga_install_files() {   # amiga_install_files <variant: aga|ecs> <datadisks-default>
	local variant="$1" ndata="${2:-6}" vol kickreq
	[ "$variant" = aga ] && vol=OpenUA-AGA || vol=OpenUA-ECS-1
	# Measured minimums (#43), not guesses — see docs/TODO.md and HARDWARE.md.
	[ "$variant" = aga ] && kickreq="Kickstart 3.0 or later (AGA)" \
	                     || kickreq="Kickstart 2.0 or later (ECS/OCS; 1.3 will NOT work)"
	# ★ WB 3.1 NEVER SHIPPED Installer (it was a separate developer package
	# that vendors bundled under a signed paper licence from AMIGA Technologies
	# — not something we can do), so the icon falls back to OUR instdisk,
	# which rides on this disk for exactly that case. Shipping the Aminet
	# Installer-43_3 binary is NOT an option: clause B.10 of its licence.
	[ -x "$REPO/instdisk_amiga" ] || { echo "mkhwdist: $REPO/instdisk_amiga missing — run: make instdisk-amiga" >&2; exit 1; }
	[ -x "$REPO/uaconv_amiga" ] || { echo "mkhwdist: $REPO/uaconv_amiga missing — run: make uaconv-amiga" >&2; exit 1; }
	[ -f "$REPO/uaconv.info" ] || { echo "mkhwdist: $REPO/uaconv.info missing — run: make uaconv.info" >&2; exit 1; }
	cp "$REPO/instdisk_amiga" "$WORK/instdisk"
	cp "$REPO/uaconv_amiga" "$WORK/uaconv"
	cp "$REPO/uaconv.info" "$WORK/uaconv.info"
	cat > "$WORK/Install" <<EOS
; OpenUA Workbench installer launcher (run by IconX).
; No .KEY line: the script takes no arguments, and a BARE .KEY is rejected
; by the 3.1 (and earlier) script runner with "Illegal Key directive" —
; seen on an A500 under WB 3.1, 2026-08-22. 3.2 tolerated it.
;
; CD OFF THE FLOPPY FIRST. This script's current directory is the engine
; disk's VOLUME, and when the Installer (or instdisk) hands control back the
; shell re-validates that directory for the next line — "Please insert
; volume ${vol}" at the very END of a successful install, with this window
; stuck open (A1200, WB 3.2 Installer 47.19, 2026-08-23). Every path below
; is absolute for the same reason; instdisk finds the drive via PROGDIR:.
;
; ★ THE CONSOLE IS NOT WASTED SPACE. IconX opens this window (see the
; WINDOW= tooltype on Install.info) and, when the AmigaOS Installer is
; present, the launcher previously left it EMPTY behind the Installer's
; GUI — a blank window whose only content was the Installer's own version
; banner. A1200 field report, 2026-09-03: "the extra window that pops up
; with the installer version in it could be used for ... some of the
; readme information / license and required files list". So print the
; things a first-time installer actually needs, while the GUI is up. It
; costs nothing at run time and needs no extra files on a full floppy.
;
; Keep it inside 78 columns and ~20 rows: the window is CON:0/20/640/180
; and topaz 8 puts 80x22 characters in that, so anything longer scrolls
; the useful lines away before the Installer's requester is answered.
ECHO ""
ECHO "  OpenUA ${VERSION} - an open reimplementation of SSI's"
ECHO "  Unlimited Adventures (FRUA, 1993) for the Amiga (${vol})"
ECHO ""
ECHO "  YOU WILL NEED"
ECHO "    - this engine disk, and your OpenUA DATA disks"
ECHO "    - about 7 MB free on the destination drive"
ECHO "    - ${kickreq}"
ECHO ""
ECHO "  THE INSTALLER WILL ASK"
ECHO "    - where to put the OpenUA drawer (it creates one for you)"
ECHO "    - how many data disks you have, then for each in turn"
ECHO "  RETURN accepts the default at every prompt."
ECHO ""
ECHO "  ENGINE: GPL-2.0, source at github.com/slaapliedje/OpenUA."
ECHO "  GAME DATA IS NOT INCLUDED and is not ours to give: the data"
ECHO "  disks are built from YOUR OWN copy of FRUA. See HARDWARE.txt."
ECHO ""
CD RAM:
IF EXISTS SYS:System/Installer
  SYS:System/Installer SCRIPT ${vol}:Install.script APPNAME OpenUA MINUSER NOVICE DEFUSER AVERAGE
ELSE
  IF EXISTS SYS:Utilities/Installer
    SYS:Utilities/Installer SCRIPT ${vol}:Install.script APPNAME OpenUA MINUSER NOVICE DEFUSER AVERAGE
  ELSE
    ECHO "This machine has no AmigaOS Installer (Workbench 3.1 never shipped one),"
    ECHO "so OpenUA's own installer runs instead. Answer its questions in this window;"
    ECHO "RETURN accepts the default. It notices disk swaps by itself."
    ECHO ""
    ${vol}:instdisk
    ECHO ""
    ECHO "Press RETURN to close this window."
    ASK "" >NIL:
  ENDIF
ENDIF
EOS
	{
		echo '; OpenUA install script for the AmigaOS Installer (2.0+)'
		echo '; Installs the engine from this disk set and the game data disks.'
		echo '(set @default-dest'
		echo '  (askdir (prompt "Where should the OpenUA drawer be created?")'
		echo '          (help "A drawer named OpenUA is created inside the drawer you pick. The game data (about 7 MB) and the engine go there.")'
		echo '          (default "DH0:") (newpath)))'
		echo '(set uadir (tackon @default-dest "OpenUA"))'
		echo '(makedir uadir (infos))'
		echo '(message "Installing the engine into " uadir)'
		if [ "$variant" = aga ]; then
			echo '(copyfiles (source "OpenUA-AGA:frua") (dest uadir) (infos))'
			echo '(copyfiles (source "OpenUA-AGA:uainst") (dest uadir) (infos))'
			echo '(copyfiles (source "OpenUA-AGA:uaconv") (dest uadir) (infos))'
		else
			echo '(copyfiles (source "OpenUA-ECS-1:frua.00") (dest uadir))'
			echo '(copyfiles (source "OpenUA-ECS-1:uainst") (dest uadir) (infos))'
			echo '(copyfiles (source "OpenUA-ECS-1:uaconv") (dest uadir) (infos))'
			echo '(copyfiles (source "OpenUA-ECS-1:frua.info") (dest uadir))'
			echo '(askdisk (prompt "Insert OpenUA engine disk 2 of 2") (help "The second half of the engine is on it.") (dest "OpenUA-ECS-2"))'
			echo '(copyfiles (source "OpenUA-ECS-2:frua.01") (dest uadir))'
			echo '(run (cat "Join " (tackon uadir "frua.00") " " (tackon uadir "frua.01") " AS " (tackon uadir "frua")))'
			echo '(run (cat "Protect " (tackon uadir "frua") " +e"))'
			echo '(delete (tackon uadir "frua.00"))'
			echo '(delete (tackon uadir "frua.01"))'
		fi
		echo "(set ndisks (asknumber (prompt \"How many OpenUA DATA disks do you have?\")"
		echo '  (help "The game data set is numbered OpenUA-Data-1, -2, ... The number is on the first line of DISK.LST on any of them.")'
		echo "  (default $ndata)))"
		echo '(set i 1)'
		echo '(while (<= i ndisks)'
		echo '  ('
		echo '    (askdisk (prompt (cat "Insert OpenUA data disk " i " of " ndisks))'
		echo '             (help "The numbered game-data disks made with mkdatadisks.")'
		echo '             (dest (cat "OpenUA-Data-" i)))'
		echo '    (copyfiles (source (cat "OpenUA-Data-" i ":")) (dest uadir) (pattern "~(DISK.LST|instdisk)"))'
		echo '    (set i (+ i 1))'
		echo '  )'
		echo ')'
		# Ask for the ENGINE disk back before exiting: the Workbench
		# launcher's remaining IF/ENDIF lines stream from that disk's
		# `Install` file, and exiting with a data disk inserted makes the
		# shell demand the volume back — or, on a bad re-read, error with
		# "ENDIF/ELSE missing" (A500 field report, 2026-08-23). Installer's
		# own askdisk is the well-tested way to get it back in the drive.
		if [ "$variant" = aga ]; then
			echo '(askdisk (prompt "Put the OpenUA engine disk (OpenUA-AGA) back in the drive.") (help "The install window needs its script disk back to close cleanly.") (dest "OpenUA-AGA"))'
		else
			echo '(askdisk (prompt "Put OpenUA engine disk 1 (OpenUA-ECS-1) back in the drive.") (help "The install window needs its script disk back to close cleanly.") (dest "OpenUA-ECS-1"))'
		fi
		echo '(complete 100)'
		echo '(exit "OpenUA is installed. Open the OpenUA drawer and double-click frua.")'
	} > "$WORK/Install.script"
	python3 "$REPO/tools/make_amiga_icon.py" --type project --default-tool "IconX" \
	    --tooltype "WINDOW=CON:0/20/640/180/OpenUA install/CLOSE" \
	    -o "$WORK/Install.info"
}

say "Amiga AGA (A1200/A4000) — 880 KB, binary raw"
readme "$WORK/README" "Amiga AGA (A1200 / A4000)" \
	"frua    the engine. Needs Kickstart 3.0+ and about 4 MB." \
	"uainst  installs a DOS fan module from its ZIP." \
	"uaconv  (optional) pre-converts all DOS art + reclaims ~5 MB (-d)." \
	"" \
	"TO INSTALL: double-click the Install icon (Workbench). It uses the" \
	"AmigaOS Installer if the machine has one; Workbench 3.1 never shipped" \
	"it, so there it runs OpenUA's own instdisk (on this disk) in a window." \
	"Or run instdisk from data disk 1 — it installs the data and then asks" \
	"for this disk to install the engine."
amiga_install_files aga 6
printf '1 1 OpenUA engine (AGA)\nfrua frua\nfrua.info frua.info\nuainst uainst\nuainst.info uainst.info\nuaconv uaconv\nuaconv.info uaconv.info\n' > "$WORK/ENGINE.LST"
cp "$WORK/uaconv" "$AGA/uaconv"; cp "$WORK/uaconv.info" "$AGA/uaconv.info"
amiga_img "$OUT/openua-amiga-aga-$VERSION.adf" "OpenUA-AGA" "$AGA" \
	frua frua.info uainst uainst.info uaconv uaconv.info
for f in README ENGINE.LST Install Install.info Install.script instdisk; do
	"$XDFTOOL" "$OUT/openua-amiga-aga-$VERSION.adf" write "$WORK/$f" >/dev/null
done

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
	"in the C startup before the program begins. Native 32-colour." \
	"" \
	"TO INSTALL: double-click the Install icon (Workbench). It uses the" \
	"AmigaOS Installer if the machine has one; Workbench 3.1 never shipped" \
	"it, so there it runs OpenUA's own instdisk (on this disk) in a window." \
	"Or run instdisk from data disk 1 — it installs the data, then asks for" \
	"this disk and disk 2 and joins the halves itself."
amiga_install_files ecs 6
printf '1 2 OpenUA engine (ECS)\nfrua.00 frua\nfrua.info frua.info\nuainst uainst\nuainst.info uainst.info\nuaconv uaconv\nuaconv.info uaconv.info\n' > "$WORK/ENGINE.LST"
amiga_img "$OUT/openua-amiga-ecs-$VERSION-disk1.adf" "OpenUA-ECS-1" "$WORK" \
	frua.00 README ENGINE.LST Install Install.info Install.script instdisk
cp "$WORK/uaconv" "$ECS/uaconv"; cp "$WORK/uaconv.info" "$ECS/uaconv.info"
for f in uainst uainst.info frua.info uaconv uaconv.info; do
	"$XDFTOOL" "$OUT/openua-amiga-ecs-$VERSION-disk1.adf" write "$ECS/$f" >/dev/null
done

readme "$WORK/README" "Amiga ECS / OCS — disk 2 of 2" \
	"frua.01 is the second half of the engine. See disk 1's README for the" \
	"three-line Join recipe."
printf '2 2 OpenUA engine (ECS)\nfrua.01 frua a\n' > "$WORK/ENGINE.LST"
amiga_img "$OUT/openua-amiga-ecs-$VERSION-disk2.adf" "OpenUA-ECS-2" "$WORK" \
	frua.01 README ENGINE.LST

# ---- Amiga hard-drive packages (.lha) ------------------------------------
# For hard-disk/CF setups and launchers (iGame, AGS): a ready-to-extract
# Workbench drawer — engine + icons + the two optional tools + a README that
# says "drop your FRUA data files in here". .lha because that is the one
# format every Amiga HD setup extracts natively (it is the Aminet standard).
# Built with `lha-make` (LHa for UNIX built from jca02266/lha — the distro
# `lha` is lhasa, which EXTRACTS but cannot create); skipped with a warning
# when absent so floppy-image builds still work anywhere.
LHAMK="$(command -v lha-make || true)"
if [ -z "$LHAMK" ]; then
	echo "mkhwdist: lha-make not found - skipping the .lha HD packages" >&2
else
	for variant in aga ecs; do
		case $variant in
		aga) SRCD="$AGA"; LOUT="$OUT/openua-amiga-$VERSION.lha"
		     LREQ="an AGA machine (A1200/A4000) or RTG (Picasso96/CyberGraphX,"
		     LREQ2="incl. Vampire/SAGA setups). Kickstart 3.0+ and about 4 MB." ;;
		ecs) SRCD="$ECS"; LOUT="$OUT/openua-amiga-ecs-$VERSION.lha"
		     LREQ="an ECS or OCS Amiga (A500+/A600/A2000/A3000) with a hard"
		     LREQ2="disk or CF. Kickstart 2.0+ and 2 MB. Native 32-colour." ;;
		esac
		HD="$WORK/hd-$variant"; rm -rf "$HD"; mkdir -p "$HD/OpenUA"
		cp "$SRCD/frua" "$SRCD/frua.info" "$SRCD/uainst" "$SRCD/uainst.info" "$HD/OpenUA/"
		cp "$WORK/uaconv" "$WORK/uaconv.info" "$HD/OpenUA/"
		[ -f "$SRCD/RELEASE.TXT" ] && cp "$SRCD/RELEASE.TXT" "$HD/OpenUA/"
		python3 "$REPO/tools/make_amiga_icon.py" --type drawer -o "$HD/OpenUA.info" >/dev/null
		python3 "$REPO/tools/make_amiga_icon.py" --type project \
		    --default-tool "SYS:Utilities/MultiView" -o "$HD/OpenUA/README.info" >/dev/null
		cat > "$HD/OpenUA/README" <<TXT
OpenUA $VERSION — hard-drive package
====================================

An open reimplementation of SSI's Unlimited Adventures engine.
This variant needs $LREQ
$LREQ2

This package contains NO game data. You supply the files from your own
legally-obtained copy of Unlimited Adventures — the DOS release's files
work AS THEY ARE (the engine converts the DOS art itself, the first time
each file is used), and the Mac release works too (see the project README
for building frua.rsc from it).

TO SET UP
---------
1. Extract this archive where your games live. You get this OpenUA drawer.
2. Copy your FRUA data files INTO the drawer (everything from the DOS
   FRUA directory: frua.rsc or the DOS files, the .TLB art, GAME.*,
   *.DSN design folders, XMI music, ...).
3. Double-click frua. That is all — first visits to screens convert
   their art once, then never again.

LAUNCHERS (iGame and friends)
-----------------------------
Point your launcher at this drawer; frua is the tool to run. The drawer
and tool icons are standard, so directory scanners index it as-is.

ALSO IN THE DRAWER
------------------
uainst  — installs a DOS FAN MODULE from its .zip, converting its art.
uaconv  — OPTIONAL: pre-convert all DOS art in one pass and reclaim
          ~5 MB (uaconv -d <this drawer>). Never required.

Project: https://github.com/slaapliedje/OpenUA (GPL-2.0, no SSI data).
TXT
		rm -f "$LOUT"
		( cd "$HD" && find OpenUA.info OpenUA -type f | sort \
		    | xargs "$LHAMK" aq2 "$LOUT" )
		say "$(basename "$LOUT")  $(stat -c%s "$LOUT") bytes  ($(find "$HD" -type f | wc -l) files)"
	done
fi

# ---- the FlashFloppy geometry stanzas ------------------------------------
# A Gotek reads a raw .img/.st by SIZE; anything that is not a standard floppy
# size needs its geometry declared. Copy this file to the root of the Gotek's
# USB stick. Stanzas taken from phjanderson/flashfloppy-atari-disks.
#
# The trick in one line: `rate` stays 250 (the WD1772 cannot go faster) and
# `rpm` comes DOWN instead, so more sectors fit under the head per revolution.
cat > "$OUT/IMG.CFG" <<'CFG'
# OpenUA — FlashFloppy geometry for the Gotek images.
# Copy to the ROOT of the Gotek's USB stick, next to the .st files.
#
# A stock Atari ST is locked to a 250 kbit/s data rate, so it cannot read real
# HD media. FlashFloppy gets past that by slowing the emulated ROTATION rather
# than raising the rate, and by allowing up to 255 cylinders.

# openua-st-gotek-*.st and openua-falcon-*.st — 1.44 MB, half speed.
# The Falcon/TT read this natively; on a stock ST the 150 rpm is what makes it
# work. Random access ~1/2 of a real floppy.
[::1474560]
cyls = 80
heads = 2
secs = 18
bps = 512
rate = 250
rpm = 150

# openua-data-gotek-disk1.st — the WHOLE game data set on ONE image
# (255 cylinders, quarter speed). Replaces the six 1.44 MB data disks.
[::9400320]
cyls = 255
heads = 2
secs = 36
bps = 512
rate = 250
rpm = 75
CFG
say "IMG.CFG written (copy it to the Gotek stick's root)"

say "done:"
ls -la "$OUT"
