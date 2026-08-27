#!/usr/bin/env bash
#
# mkdatadisks.sh — spread a staged FRUA data directory over floppy images.
#
#   tools/mkdatadisks.sh <atari|amiga> [gamedata-dir] [outdir] [design...]
#
# ★★ THE OUTPUT CONTAINS COPYRIGHTED GAME DATA. It is written under data/,
#    which .gitignore excludes, and it must NEVER be committed or attached to
#    a GitHub release. The engine disk images (tools/mkhwdist.sh) are the
#    redistributable ones; these are for the owner of the data only.
#
# A minimum playable install is ~7.4 MB, which is why this exists: six 1.44 MB
# Atari disks or nine 880 KB Amiga disks. Every individual FRUA file is small
# (the largest art library is under 300 KB), so files are packed whole and
# nothing needs splitting or joining — the packer just bin-packs them.
#
# Disk 1 also carries the installer (INSTDISK.TTP / instdisk) and each disk
# carries DISK.LST, the manifest instdisk copies from. See installer/instdisk.c
# for why it is manifest-driven rather than reading the directory.
#
set -euo pipefail

MACHINE="${1:?usage: mkdatadisks.sh <atari|atari720|amiga|gotek> [gamedata-dir] [outdir] [design...]}"
# ART=tlb (default) | ctl | both
#
# ★ THE ART EXISTS IN TWO FORMATS and a staged directory holds both: the DOS
#   original `.TLB` (HLIB) and the Mac `.ctl` (GLIB) twin our converter derives
#   from it. 23 pairs, 3.60 MB and 3.36 MB of a 7.35 MB tree — the same
#   pictures, counted twice. Shipping both is what made these sets twice as
#   long as SSI's own three-disk release.
#
#   DEFAULT IS `tlb`: SSI's DOS FILES, COPIED STRAIGHT ACROSS. Nothing is
#   converted on the way to the disks, so what you install is what SSI shipped.
#   The engine converts on FIRST TOUCH and writes the `.ctl` back beside it
#   (ADR-0014) — 48 ms median / ~1.5 s for a typical picture on an 8 MHz 68000,
#   6 ms / ~0.3 s on the Falcon, once per library ever.
#
#   Budget for that: the destination ends up holding BOTH formats, ~7.4 MB, not
#   the ~4 MB the disks carry.
#
#   This only became viable once ua_open_art's ROOT FALLBACK learned to convert
#   too. Until then a .TLB-only install rendered pictures but no text, because
#   BACK/TITLE/MENU resolve through the fallback and it was a bare FSOpen. A
#   .TLB-only install now renders PIXEL-IDENTICAL to the .ctl set.
#
#   ART=ctl ships the converted Mac-format art instead: one disk fewer and no
#   first-touch pause, but the files are then ours rather than SSI's — and only
#   14 of 23 are byte-identical to the genuine Mac release, the rest differing
#   because the DOS and Mac art genuinely differ.
#
#   ART=both is the old behaviour and doubles the disks. Use it to revive the
#   MONO build, which treats an HLIB `.tlb` as a deliberate miss (41-53 s per
#   wall master is installer work, not first-touch work).
#
# ★ THE DEFAULT IS MACHINE-DEPENDENT. Everything above about .tlb converting on
#   first touch is the ATARI engine — its ua_open_art root fallback runs the
#   converter (boot.c, guarded `!defined(FRUA_AMIGA)`). The AMIGA engine does
#   NOT: ADR-0015 compiled the on-the-fly conversion OUT (art is install-time
#   work there), and instdisk copies verbatim — so a .tlb-only Amiga install has
#   no path to the .ctl the engine loads (ALWAYS.CTL fails first: "Bad Lib",
#   then the cold disk-swap dialog — the A500 "Please insert disk" seen
#   2026-08-22). Amiga therefore defaults to `ctl` (ship the converted libs);
#   Atari keeps `tlb`. Override either with ART= on the command line.
# Every target now ships DOS .tlb. Atari converts on first touch (cached);
# Amiga converts once at install via uaconv (installer/uaconv.c) — the engine
# there cannot convert (ADR-0015). ART=ctl still ships pre-converted libs if
# you would rather skip the install-time conversion.
ART="${ART:-tlb}"
case "$ART" in tlb|ctl|both) ;; *) echo "ART must be tlb, ctl or both" >&2; exit 1 ;; esac

# PERBAND=<colours> — pre-reduce the art to the PER-BAND colour budget
# (ADR-0020 v2, tools/perband.py) before packing. OFF by default.
#
# What it buys: on a 16/32-colour bitplane machine the runtime quantiser cuts
# every scene on the fly, which is where the per-band seams, the stray
# wrong-slot pixels and the re-band CPU all come from. Art that already fits
# the hardware's per-band budget quantises EXACTLY — measured on the ECS
# tavern picture, the remaining band artefacts went 3 rows to ZERO — and the
# quantiser has nothing left to do.
#
# Suggested budgets (leave headroom below the hardware's per-band maximum for
# the frame chrome, roster and text box that share every band):
#     24   Amiga ECS   (32 colours per copper band)
#     12   Atari ST/STE (16 colours)
#
# ★ WHY THIS IS OPT-IN AND NOT A DEFAULT. One set of data disks serves a whole
#   family: the `atari` disks feed the ST *and* the Falcon/TT, the `amiga`
#   disks feed ECS *and* AGA. Reducing the art helps the machine that has to
#   quantise and very slightly flattens the one that does not (Falcon, AGA and
#   the graphics cards are 256-colour and want SSI's palettes untouched). So
#   converting is a deliberate choice made when you are building media for a
#   specific low-colour machine, not something done to everyone's art by
#   default. Build a second set with PERBAND= set for the ST or the A500.
#
# Works with ART=tlb: the converter reads SSI's DOS .TLB directly (the palette
# payload is identical to its .ctl twin), so the engine's on-first-touch
# conversion carries the reduced palettes through unchanged.
PERBAND="${PERBAND:-}"
if [[ -n "$PERBAND" ]] && ! [[ "$PERBAND" =~ ^[0-9]+$ ]]; then
	echo "PERBAND must be a colour count (e.g. PERBAND=24)" >&2; exit 1
fi
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${2:-$REPO/data/work/gamedata}"
OUT="${3:-$REPO/data/work/diskimages}"
shift $(( $# < 3 ? $# : 3 )) || true
DESIGNS=("$@")
[[ ${#DESIGNS[@]} -eq 0 ]] && DESIGNS=(HEIRS.DSN)

# unit = allocation unit in bytes, cap = usable units, root = max root-directory
# entries (0 = no fixed limit), fmt = mformat size. All four MEASURED, not
# assumed — see the note below.
case "$MACHINE" in
atari)     UNIT=512;  CAP=2840; ROOT=224; FMT=1440; EXT=st
           INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP
           INST2="$REPO/instdisk.prg";  INST2NAME=INSTDISK.PRG ;;
atari720)  UNIT=1024; CAP=709;  ROOT=112; FMT=720;  EXT=st
           INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP
           INST2="$REPO/instdisk.prg";  INST2NAME=INSTDISK.PRG ;;
amiga)     UNIT=512;  CAP=1740; ROOT=0;   FMT=;     EXT=adf
           INST="$REPO/instdisk_amiga"; INSTNAME=instdisk
           INST2="";                    INST2NAME= ;;
# ★ gotek — ONE image instead of six, on a FlashFloppy Gotek.
#
# A stock ST's WD1772 is stuck at 250 kbit/s, which is why the 1.44 MB and
# 2.88 MB media it cannot read stayed off the table. FlashFloppy sidesteps that
# without touching the data rate: it SLOWS THE EMULATED ROTATION so more
# sectors pass the head per revolution, and it will serve up to 255 cylinders.
# At 255 cyls x 2 heads x 36 sectors that is 9,400,320 bytes at 75 rpm — a
# quarter of a real floppy's random-access speed, which costs nothing for a
# one-off copy to a hard disk. Geometry and rpm come from phjanderson's
# flashfloppy-atari-disks IMG.CFG; `mkhwdist.sh` writes the matching stanza.
#
# MEASURED from the BPB mformat produces, not assumed: 512-byte sectors,
# 4096-byte clusters, 512 root entries, 18360 total sectors, 2289 clusters
# (comfortably under FAT12's 4085-cluster ceiling, so TOS still sees FAT12).
gotek)     UNIT=4096; CAP=2289; ROOT=512; FMT=;     EXT=st
           MGEOM="-t 255 -h 2 -n 36"
           INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP
           INST2="$REPO/instdisk.prg";  INST2NAME=INSTDISK.PRG ;;
*)         echo "unknown machine: $MACHINE (atari|atari720|amiga|gotek)" >&2; exit 1 ;;
esac
MGEOM="${MGEOM:-}"
# ★ PACK BY BLOCKS, NOT BYTES. A byte budget with a flat overhead subtracted
# looks right and silently overflows on a disk with MANY files: each file costs
# a rounded-up block regardless of size, and on FFS an extra header block plus
# an extension block per 72 data blocks. A 60-file disk that measured 855,717
# bytes — comfortably under an 856,064-byte cap — actually needed 1774 blocks
# of the 1756 that exist, and the run died mid-set on a write nobody checked.
#
#   Atari 1.44MB FAT12  1-sector clusters,  1,457,664 free -> 2847, use 2840
#   Atari 720K  FAT12   2-sector clusters,    730,112 free ->  713, use 709
#   Amiga 880K  FFS     512-byte blocks, 1760 total less root/bitmap -> 1740
#
# ★ 720K IS NOT JUST "HALF OF 1.44". Its cluster is 1024 bytes, not 512, so
#   small files waste twice as much, AND its root directory holds 112 entries
#   against 1.44MB's 224. With ~118 data files the ROOT LIMIT binds before
#   capacity does, so the packer budgets entries as well as blocks. Measured:
#   111 files fit in a 720K root (112 slots less the volume label), 110 with a
#   subdirectory present.

XDFTOOL="$REPO/tools/.venv/bin/xdftool"
say() { echo "datadisks: $*"; }

[[ -d "$SRC" ]] || { echo "no game data at $SRC" >&2; exit 1; }
[[ -f "$INST" ]] || { echo "installer not built: $INST" >&2
                      echo "  make instdisk        (Atari)" >&2
                      echo "  make instdisk-amiga  (Amiga)" >&2; exit 1; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
rm -f "$OUT"/openua-data-"$MACHINE"-*."$EXT"

# ---- the file list: base game (root files) + the named designs -------------
: > "$WORK/all"
# The ENGINE is not data — it ships in the release zips and on the mkhwdist
# images, and a dev build is 4.6 MB. It stayed off these disks only because
# `make gamedata` happens to symlink it (and -type f skips symlinks), which is
# luck, not intent: a real frua.prg in the staging dir would have been packed.
( cd "$SRC" && find . -maxdepth 1 -type f ! -name '*.EXE' ! -name 'DBG.LOG' \
	! -name 'frua.prg' ! -name 'frua' ! -name '*.CHR' ! -name '*.CCH' \
	-printf '%P\n' ) >> "$WORK/all"
if [[ -n "$PERBAND" ]]; then
	# Convert a COPY, never the user's staged tree.
	say "per-band art conversion, budget $PERBAND colours ..."
	PBSRC="$WORK/perband-src"
	cp -a "$SRC" "$PBSRC"
	python3 "$REPO/tools/perband.py" "$PBSRC" --out "$PBSRC" \
		--budget "$PERBAND" | sed 's/^/  /'
	SRC="$PBSRC"
fi

for d in "${DESIGNS[@]}"; do
	[[ -d "$SRC/$d" ]] || { echo "no such design: $SRC/$d" >&2; exit 1; }
	# Saved games are the player's own data, EXCEPT the one that ships on the
	# original HEIRS disks: Save A. Drop every savgam*/vault* wherever it sits
	# (the .DSN root held headless-test cruft; SAVE/ held B/H/I test saves),
	# then re-add ONLY SAVE/SAVGAMA.CSV + SAVE/VAULTA.DAT — the authentic Save A.
	( cd "$SRC" && find "$d" -type f \
		! -iname 'savgam*' ! -iname 'vault*' ! -name '*.CCH' \
		-printf '%p\n' ) >> "$WORK/all"
	[[ -f "$SRC/$d/SAVE/SAVGAMA.CSV" ]] && echo "$d/SAVE/SAVGAMA.CSV" >> "$WORK/all"
	[[ -f "$SRC/$d/SAVE/VAULTA.DAT"  ]] && echo "$d/SAVE/VAULTA.DAT"  >> "$WORK/all"
done
case "$ART" in
ctl) grep -v '\.TLB$'  "$WORK/all" > "$WORK/all.f" && mv "$WORK/all.f" "$WORK/all" ;;
tlb) grep -v '\.ctl$'  "$WORK/all" > "$WORK/all.f" && mv "$WORK/all.f" "$WORK/all" ;;
esac
sort -o "$WORK/all" "$WORK/all"
TOTBYTES=$( (cd "$SRC" && tr '\n' '\0' < "$WORK/all" | xargs -0 stat -c%s) | awk '{s+=$1} END {print s}')
say "$(wc -l < "$WORK/all") files, $TOTBYTES bytes (ART=$ART)"

# ---- bin-pack into disks ---------------------------------------------------
# ★ Disk 1 also carries the installer, so its usable capacity is smaller. Not
# accounting for that packed disk 1 to the full cap and then failed at mcopy
# time with a bare "Disk full", after the image was already written.
INSTSZ=$(stat -c%s "$INST")
if [[ -n "${INST2:-}" ]]; then
	[[ -f "$INST2" ]] || { echo "installer not built: $INST2 (make instdisk)" >&2; exit 1; }
	INSTSZ=$(( INSTSZ + $(stat -c%s "$INST2") ))
fi
python3 - "$SRC" "$WORK" "$CAP" "$INSTSZ" "$MACHINE" "$UNIT" "$ROOT" <<'PY'
import os, sys
src, work, capblocks = sys.argv[1], sys.argv[2], int(sys.argv[3])
instsz, machine = int(sys.argv[4]), sys.argv[5]
unit, rootmax = int(sys.argv[6]), int(sys.argv[7])

def blocks(nbytes):
    """Allocation units a file costs, filesystem overhead included."""
    data = (nbytes + unit - 1) // unit
    if machine == 'amiga':
        return data + 1 + data // 72   # + file header, + FFS extension blocks
    return max(data, 1)                # FAT12: clusters; the directory entry
                                        # is counted separately against rootmax

files = [l.rstrip('\n') for l in open(os.path.join(work, 'all')) if l.strip()]
files.sort(key=lambda f: -os.path.getsize(os.path.join(src, f)))
disks, used = [], []
# disk 1 also carries the installer; every disk carries DISK.LST and, if any of
# its files live in a design folder, that folder's directory block.
def budget(i):
    b = capblocks - blocks(4096)                    # DISK.LST + slack
    if i == 0:
        b -= blocks(instsz)
    return b

def entry_budget(i):
    """Root-directory entries available. 0 = unlimited (Amiga FFS chains)."""
    if rootmax == 0:
        return 10**9
    # less the volume label, DISK.LST, and the installer on disk 1
    return rootmax - 2 - (1 if i == 0 else 0)

entries = []
for f in files:
    n = blocks(os.path.getsize(os.path.join(src, f)))
    sub = os.sep in f
    n += 1 if sub else 0                            # room for the design dir
    for i, u in enumerate(used):
        # a file inside a design folder costs a ROOT entry only for the folder
        # itself, and only the first time that disk sees one
        e = 0 if sub and entries[i][1] else (1 if not sub else 1)
        if u + n <= budget(i) and entries[i][0] + e <= entry_budget(i):
            disks[i].append(f); used[i] += n
            entries[i][0] += e
            if sub:
                entries[i][1] = True
            break
    else:
        disks.append([f]); used.append(n); entries.append([1, sub])
for i, d in enumerate(disks, 1):
    with open(os.path.join(work, f'disk{i}.lst'), 'w') as fh:
        fh.write(f'{i} {len(disks)} OpenUA game data\n')
        for f in sorted(d):
            fh.write(f.replace(os.sep, '/') + '\n')
pass
PY
NDISKS=$(ls "$WORK"/disk*.lst | wc -l)
say "packing into $NDISKS x $MACHINE disks"

# ---- build the images ------------------------------------------------------
for ((n = 1; n <= NDISKS; n++)); do
	IMG="$OUT/openua-data-$MACHINE-disk$n.$EXT"
	STAGE="$WORK/stage$n"; mkdir -p "$STAGE"
	# stage this disk's files, preserving the one level of .DSN nesting
	while read -r f; do
		[[ -z "$f" ]] && continue
		mkdir -p "$STAGE/$(dirname "$f")"
		cp "$SRC/$f" "$STAGE/$f"
	done < <(tail -n +2 "$WORK/disk$n.lst")
	cp "$WORK/disk$n.lst" "$STAGE/DISK.LST"
	[[ $n -eq 1 ]] && cp "$INST" "$STAGE/$INSTNAME"
	[[ $n -eq 1 && -n "${INST2:-}" ]] && cp "$INST2" "$STAGE/$INST2NAME"

	# Branch on the IMAGE FORMAT, not the machine name: `atari720` is an Atari
	# target too, and matching == atari silently sent it down the Amiga path to
	# xdftool, which produced an FFS filesystem inside a file called .st.
	if [[ "$EXT" == st ]]; then
		rm -f "$IMG"
		if [[ -n "$MGEOM" ]]; then
			mformat -C $MGEOM -v OPENUADAT -i "$IMG" ::
		else
			mformat -C -f "$FMT" -v OPENUADAT -i "$IMG" ::
		fi
		( cd "$STAGE" && for e in *; do
			if [[ -d "$e" ]]; then
				mmd -i "$IMG" "::/$e"
				mcopy -s -i "$IMG" "$e"/* "::/$e/"
			else
				mcopy -i "$IMG" "$e" ::/
			fi
		done )
		free=$(mdir -i "$IMG" :: | grep -F 'bytes free' | tr -s ' ')
	else
		rm -f "$IMG"
		"$XDFTOOL" "$IMG" create + format "OpenUA-Data-$n" ffs >/dev/null
		# ★ Check every write. Swallowing xdftool's status left the run
		# dying mid-set with no message and a half-written image on disk
		# — the same silent-failure shape the Atari cap bug had.
		( cd "$STAGE"
			# dirs shallowest-first (HEIRS.DSN before HEIRS.DSN/SAVE)
			find . -mindepth 1 -type d | sed 's|^\./||' | sort | while read -r d; do
				"$XDFTOOL" "$IMG" makedir "$d" >/dev/null \
					|| { echo "  makedir FAILED: $d" >&2; exit 1; }
			done
			find . -type f | sed 's|^\./||' | while read -r g; do
				gd=$(dirname "$g")
				if [[ "$gd" == "." ]]; then
					"$XDFTOOL" "$IMG" write "$g" >/dev/null \
						|| { echo "  write FAILED: $g" >&2; exit 1; }
				else
					"$XDFTOOL" "$IMG" write "$g" "$gd" >/dev/null \
						|| { echo "  write FAILED: $g" >&2; exit 1; }
				fi
			done )
		free=$("$XDFTOOL" "$IMG" info | awk '/^free:/ {print $3" free"}')
	fi
	say "$(basename "$IMG")  $(tail -n +2 "$WORK/disk$n.lst" | wc -l) files —$free"
done

# Per-machine name: the three sets share an output directory, so a single
# README-DATA.txt meant whichever target ran last silently replaced the
# instructions for the other two — and they differ (disk counts, installer
# name, destination syntax).
# ${X:-else} is NOT an else arm: when X is set it expands to X itself, which
# appended a stray "INSTDISK.PRG" to the Atari READMEs. Build the phrase first.
if [[ -n "${INST2NAME:-}" ]]; then
	INSTDESC=" ($INST2NAME for the desktop, $INSTNAME for the console)"
else
	INSTDESC=": $INSTNAME"
fi
cat > "$OUT/README-DATA-$MACHINE.txt" <<EOF
OpenUA game-data disks ($MACHINE) — $NDISKS disks

*** THESE CONTAIN COPYRIGHTED GAME DATA. Do not redistribute them. ***

Disk 1 carries the installer$INSTDESC.
Run it, give it a destination, and feed the disks in when asked:

    Atari:  C:\\OPENUA
    Amiga:  DH0:OpenUA

When the data is in, it asks for the OpenUA ENGINE disk and installs the
engine into the same directory (joining the two halves on the Amiga ECS set
itself). Run the game from that directory — it looks for frua.rsc relative
to where it runs.

Amiga Workbench users can instead double-click the Install icon on the
engine disk: it uses the OS Installer and asks for these disks by name.
EOF
say "done -> $OUT"
ls -la "$OUT"
