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

MACHINE="${1:?usage: mkdatadisks.sh <atari|amiga> [gamedata-dir] [outdir] [design...]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${2:-$REPO/data/work/gamedata}"
OUT="${3:-$REPO/data/work/diskimages}"
shift $(( $# < 3 ? $# : 3 )) || true
DESIGNS=("$@")
[[ ${#DESIGNS[@]} -eq 0 ]] && DESIGNS=(HEIRS.DSN)

case "$MACHINE" in
atari)  BLOCKS=2840; EXT=st;  INST="$REPO/instdisk.ttp";   INSTNAME=INSTDISK.TTP ;;
amiga)  BLOCKS=1740; EXT=adf; INST="$REPO/instdisk_amiga"; INSTNAME=instdisk ;;
*)      echo "unknown machine: $MACHINE (atari|amiga)" >&2; exit 1 ;;
esac
# ★ PACK BY BLOCKS, NOT BYTES. A byte budget with a flat overhead subtracted
# looks right and silently overflows on a disk with MANY files: each file costs
# a rounded-up block regardless of size, and on FFS an extra header block plus
# an extension block per 72 data blocks. A 60-file disk that measured 855,717
# bytes — comfortably under an 856,064-byte cap — actually needed 1774 blocks
# of the 1756 that exist, and the run died mid-set on a write nobody checked.
#
#   Atari 1.44MB FAT12  2880 sectors of 512, less boot/FAT/root  -> 2840 usable
#   Amiga 880K FFS      1760 blocks of 512, less root/bitmap     -> 1740 usable

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
python3 - "$SRC" "$WORK" "$BLOCKS" "$INSTSZ" "$MACHINE" <<'PY'
import os, sys
src, work, capblocks = sys.argv[1], sys.argv[2], int(sys.argv[3])
instsz, machine = int(sys.argv[4]), sys.argv[5]

def blocks(nbytes):
    """Blocks a file of this size costs, filesystem overhead included."""
    data = (nbytes + 511) // 512
    if machine == 'amiga':
        return data + 1 + data // 72   # + file header, + FFS extension blocks
    return max(data, 1)                # FAT12: clusters only; dir entry is
                                        # in the fixed-size root, counted below

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
for f in files:
    n = blocks(os.path.getsize(os.path.join(src, f)))
    n += 1 if os.sep in f else 0                    # room for the design dir
    for i, u in enumerate(used):
        if u + n <= budget(i):
            disks[i].append(f); used[i] += n; break
    else:
        disks.append([f]); used.append(n)
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

	if [[ "$MACHINE" == atari ]]; then
		rm -f "$IMG"
		mformat -C -f 1440 -v OPENUADAT -i "$IMG" ::
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

cat > "$OUT/README-DATA.txt" <<EOF
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
