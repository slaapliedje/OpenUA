#!/usr/bin/env bash
#
# mkdatadisks.sh — the old command line for tools/mkdatadisks.py.
#
#   tools/mkdatadisks.sh <atari|atari720|gotek|amiga> [gamedata-dir] [outdir] [design...]
#   ART=tlb|ctl|both  PERBAND=auto|off|N  MODULE=<Name>   (as before)
#
# The work moved to Python (tools/mkdatadisks.py, writing the images with
# tools/diskimg.py) so a player on Windows can run it from the release zip
# without mtools or amitools; this keeps the developer's defaults (the
# staged data/work/gamedata, output under data/work/diskimages). The rules
# that lived here — what goes on the disks, packing by blocks and root
# entries, Save A only, the per-band budgets, module sets — are documented
# in the Python now. The last bash version is in git history (before
# 2026-11-27), and its output was identical for all four families.
#
# ★★ THE OUTPUT CONTAINS COPYRIGHTED GAME DATA. It is written under data/,
#    which .gitignore excludes, and it must NEVER be committed or attached to
#    a GitHub release.
set -euo pipefail

MACHINE="${1:?usage: mkdatadisks.sh <atari|atari720|gotek|amiga> [gamedata-dir] [outdir] [design...]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${2:-$REPO/data/work/gamedata}"
OUT="${3:-$REPO/data/work/diskimages}"
shift $(( $# < 3 ? $# : 3 )) || true
ARGS=("$SRC" --out "$OUT" --for "$MACHINE" --installers "$REPO")
for d in "$@"; do ARGS+=(--design "$d"); done
[[ -n "${ART:-}" ]] && ARGS+=(--art "$ART")
[[ -n "${PERBAND:-}" ]] && ARGS+=(--perband "$PERBAND")
[[ -n "${MODULE:-}" ]] && ARGS+=(--module "$MODULE")
PY="$REPO/tools/.venv/bin/python3"
[[ -x "$PY" ]] || PY=python3
exec "$PY" "$REPO/tools/mkdatadisks.py" "${ARGS[@]}"
