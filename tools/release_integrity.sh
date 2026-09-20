#!/usr/bin/env bash
# release_integrity.sh <version> — check the SHIPPED zips, not a dev build.
#
#   make release-all VERSION=0.9.29-beta && tools/release_integrity.sh 0.9.29-beta
#
# Every zero-shaped failure mode aborts loudly: a missing objdump, a missing
# binary inside the zip, or a wrong path all look like "0 020-ops, stripped"
# and each has fooled a release before. The signal is the RATIO between the
# 020 and 68000 builds (hundreds-to-thousands vs a handful of phantoms from
# data disassembled as code), never an absolute count.
#
# This lived in a session scratch directory through v0.9.28 and was lost to a
# host restart; a release gate belongs in the repo.
set -u
V="${1:?usage: release_integrity.sh <version>}"
R="$(cd "$(dirname "$0")/.." && pwd)"
ATD="${MINT_OBJDUMP:-$HOME/opt/cross-mint/bin/m68k-atari-mint-objdump}"
ATN="${MINT_NM:-$HOME/opt/cross-mint/bin/m68k-atari-mint-nm}"
AOD="${AMIGA_OBJDUMP:-$HOME/opt/amiga/bin/m68k-amigaos-objdump}"
AON="${AMIGA_NM:-$HOME/opt/amiga/bin/m68k-amigaos-nm}"
for t in "$ATD" "$ATN" "$AOD" "$AON"; do
	[ -x "$t" ] || { echo "FATAL: missing tool $t"; exit 1; }
done
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
check() {   # check <zip-stem> <binary-name> <objdump> <nm> <expect: 020|68000>
	local stem=$1 bin=$2 od=$3 nm=$4 expect=$5
	local z="$R/dist/openua-$stem-$V.zip"
	[ -f "$z" ] || { echo "FAIL $stem: zip missing $z"; fail=1; return; }
	rm -rf "$T/$stem"; mkdir -p "$T/$stem"; unzip -q "$z" -d "$T/$stem"
	local b="$T/$stem/openua-$stem-$V/$bin"
	[ -f "$b" ] || { echo "FAIL $stem: NO BINARY at $b"; ls "$T/$stem"; fail=1; return; }
	local ops syms stripped ver
	ops=$("$od" -D -b binary -m m68k:68020 "$b" 2>/dev/null \
	      | grep -cE 'muls\.l|mulu\.l|divs\.l|bfextu|bfins|bfset|mulsl|mulul|divsl')
	syms=$("$nm" "$b" 2>&1 | grep -vc "no symbols")
	[ "$syms" = "0" ] && stripped=yes || stripped="NO ($syms syms)"
	ver=$(strings -a "$b" | grep -m1 -oE "OpenUA [0-9][^ ]*")
	printf "%-12s %-9s size=%8d  020ops=%5d  stripped=%-4s  stamp=%s\n" \
	       "$stem" "$bin" "$(stat -c %s "$b")" "$ops" "$stripped" "$ver"
	case $expect in
	020)   [ "$ops" -gt 200 ] || { echo "   FAIL: expected an 020 build (hundreds+), got $ops"; fail=1; } ;;
	68000) [ "$ops" -lt 40 ]  || { echo "   FAIL: expected a 68000 build (a handful of phantoms), got $ops"; fail=1; } ;;
	esac
	[ "$stripped" = yes ] || { echo "   FAIL: not stripped"; fail=1; }
	[ "$ver" = "OpenUA $V" ] || { echo "   FAIL: version stamp '$ver' != 'OpenUA $V'"; fail=1; }
}
echo "== zips for $V =="
check falcon     frua.prg "$ATD" "$ATN" 020
check atari-st   frua.prg "$ATD" "$ATN" 68000
check amiga      frua     "$AOD" "$AON" 020
check amiga-ecs  frua     "$AOD" "$AON" 68000
echo
echo "== the bundled PC-side tool actually does work (not a vacuous exit 0) =="
A="$T/amiga/openua-amiga-$V"
CTL=$(ls "$R"/data/work/gamedata/*.ctl "$R"/data/work/gamedata/*.CTL 2>/dev/null | head -1)
if [ -n "$CTL" ] && [ -f "$A/perband.py" ]; then
	cp "$CTL" "$A/probe.ctl"
	out=$(cd "$A" && python3 perband.py probe.ctl --report 2>&1 | grep -iE "converted" | head -1)
	echo "   perband on $(basename "$CTL"): $out"
	echo "$out" | grep -qE "[1-9][0-9]*" || { echo "   FAIL: perband converted nothing"; fail=1; }
else
	echo "   SKIP: no .ctl under data/work/gamedata to probe with (game data is not in the repo)"
fi
echo
[ $fail = 0 ] && echo "INTEGRITY: ALL PASS" || echo "INTEGRITY: FAILURES ABOVE"
exit $fail
