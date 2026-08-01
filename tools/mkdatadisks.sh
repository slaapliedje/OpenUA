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

MACHINE="${1:?usage: mkdatadisks.sh <atari|atari720|amiga> [gamedata-dir] [outdir] [design...]}"
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
           INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP ;;
atari720)  UNIT=1024; CAP=709;  ROOT=112; FMT=720;  EXT=st
           INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP ;;
amiga)     UNIT=512;  CAP=1740; ROOT=0;   FMT=;     EXT=adf
           INST="$REPO/instdisk_amiga"; INSTNAME=instdisk ;;
*)         echo "unknown machine: $MACHINE (atari|atari720|amiga)" >&2; exit 1 ;;
esac
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
for d in "${DESIGNS[@]}"; do
	[[ -d "$SRC/$d" ]] || { echo "no such design: $SRC/$d" >&2; exit 1; }
	( cd "$SRC" && find "$d" -type f -printf '%p\n' ) >> "$WORK/all"
done
sort -o "$WORK/all" "$WORK/all"
TOTBYTES=$( (cd "$SRC" && tr '\n' '\0' < "$WORK/all" | xargs -0 stat -c%s) | awk '{s+=$1} END {print s}')
say "$(wc -l < "$WORK/all") files, $TOTBYTES bytes"

# ---- bin-pack into disks ---------------------------------------------------
# ★ Disk 1 also carries the installer, so its usable capacity is smaller. Not
# accounting for that packed disk 1 to the full cap and then failed at mcopy
# time with a bare "Disk full", after the image was already written.
INSTSZ=$(stat -c%s "$INST")
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

	# Branch on the IMAGE FORMAT, not the machine name: `atari720` is an Atari
	# target too, and matching == atari silently sent it down the Amiga path to
	# xdftool, which produced an FFS filesystem inside a file called .st.
	if [[ "$EXT" == st ]]; then
		rm -f "$IMG"
		mformat -C -f "$FMT" -v OPENUADAT -i "$IMG" ::
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
		( cd "$STAGE" && for e in *; do
			if [[ -d "$e" ]]; then
				"$XDFTOOL" "$IMG" makedir "$e" >/dev/null \
					|| { echo "  makedir FAILED: $e" >&2; exit 1; }
				for g in "$e"/*; do
					"$XDFTOOL" "$IMG" write "$g" "$e" >/dev/null \
						|| { echo "  write FAILED: $g" >&2; exit 1; }
				done
			else
				"$XDFTOOL" "$IMG" write "$e" >/dev/null \
					|| { echo "  write FAILED: $e" >&2; exit 1; }
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
cat > "$OUT/README-DATA-$MACHINE.txt" <<EOF
OpenUA game-data disks ($MACHINE) — $NDISKS disks

*** THESE CONTAIN COPYRIGHTED GAME DATA. Do not redistribute them. ***

Disk 1 carries $INSTNAME. Run it, give it a destination, and feed the
disks in when asked:

    Atari:  C:\\OPENUA
    Amiga:  DH0:OpenUA

Then copy the engine (FRUA.PRG or frua) into that same directory and run
it from there — it looks for frua.rsc relative to where it runs.
EOF
say "done -> $OUT"
ls -la "$OUT"
