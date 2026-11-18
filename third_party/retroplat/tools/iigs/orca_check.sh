#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# orca_check.sh -- compile C files with ORCA/C (Apple IIGS) under ORCA's
# full lint, for `make check-iigs`.
#
#   tools/iigs/orca_check.sh IIX FILE.c...
#
# IIX is Golden Gate's `iix`.  On Linux that is a wrapper around Golden
# Gate's Windows build under Wine, with a sibling `iix-clean` that deletes
# the `name:AFP_AfpInfo` files Wine makes of NTFS file-type streams (a
# colon is the GS/OS path separator, so the ORCA linker trips on them).
# Only that Wine arrangement has been run; a native macOS/Windows iix
# would need the Z: path mapping below changed.  Absent iix is a SKIP,
# like every cross-toolchain.
#
# Why flat staging: ORCA/C takes no -I.  A quoted #include searches the
# CURRENT directory -- not the including file's -- and then ORCA's own
# Libraries:ORCACDefs:, which ships its own types.h, memory.h, font.h,
# event.h and window.h.  So headers and sources are copied into one
# directory and the compiler runs from inside it.  Flattening could let
# two same-named files overwrite each other, so a collision stops here.
#
# Why a control first: a passing result is "0 errors found", which is also
# what a pipeline that never compiled anything would print.  The control
# fixture must be REPORTED, or nothing after it is believed.
set -e

IIX=$1; shift

if [ ! -x "$IIX" ]; then
    echo "check-iigs: SKIP -- no Golden Gate iix at $IIX (set IIX=)"
    exit 0
fi
IIX=$(cd "$(dirname "$IIX")" && pwd)/$(basename "$IIX")
IIX_CLEAN="$(dirname "$IIX")/iix-clean"

rm -rf build/iigs; mkdir -p build/iigs/stage build/iigs/log
OUT=$(cd build/iigs && pwd)
STAGE=$OUT/stage

for f in include/*.h $(ls backends/iigs/*.h 2>/dev/null) "$@"; do
    b=$(basename "$f")
    if [ -e "$STAGE/$b" ]; then
        echo "check-iigs: FAIL -- two files named $b; flat staging would lose one"
        exit 1
    fi
    cp "$f" "$STAGE/$b"
done

# Golden Gate runs under Wine: reach the stage through Z:, Wine's root.
WSTAGE="Z:$(printf '%s' "$STAGE" | tr '/' '\\')"
[ -x "$IIX_CLEAN" ] && "$IIX_CLEAN"

compile() {
    printf '#pragma lint -1\n#include "%s.c"\n' "$1" > "$STAGE/lint_$1.c"
    (cd "$STAGE" && "$IIX" compile -I "$WSTAGE\\lint_$1.c" keep="$WSTAGE\\$1") \
        > "$OUT/log/$1.log" 2>&1 || true
    grep -o '[0-9][0-9]* errors\{0,1\} found' "$OUT/log/$1.log" | tail -1
}

printf 'int iigs_control(void) { return never_declared(3); }\n' > "$STAGE/iigs_control.c"
r=$(compile iigs_control)
case $r in
    "1 error found") echo "  control   ORCA/C reports the lint fixture, so it can speak" ;;
    *) echo "check-iigs: FAIL -- control fixture gave '${r:-no result line}', not 1 error"
       echo "  (log: build/iigs/log/iigs_control.log)"
       exit 1 ;;
esac
rm -f "$STAGE"/iigs_control* "$STAGE"/lint_iigs_control*

fail=0; n=0
for f in "$@"; do
    b=$(basename "$f" .c); n=$((n + 1))
    r=$(compile "$b")
    if [ "$r" != "0 errors found" ] || [ ! -s "$STAGE/$b.a" ]; then
        echo "  FAIL      $f: ${r:-no result line} (log: build/iigs/log/$b.log)"
        grep -B2 '\^' "$OUT/log/$b.log" | head -20
        fail=1
    fi
done
[ $fail -eq 0 ] || { echo "check-iigs: FAIL"; exit 1; }
echo "check-iigs: $n files compile clean under ORCA/C with #pragma lint -1"
