#!/bin/sh
# Regenerate docs/lxxxx-jt-aliases.md — the lXXXX <-> JT-export alias map.
#
# A CODE-local LXXXX label that sits at the same address as an `entry_jtNNN:`
# export IS that JT entry; it is very likely already lifted as jtNNN in boot.c.
# Check this map before lifting any lXXXX so you alias instead of duplicating
# (the l30bc=jt882 / l25ce=jt893 / l2f6e traps).
#
# Usage:  tools/gen_jt_aliases.sh > docs/lxxxx-jt-aliases.md
set -eu
cd "$(dirname "$0")/.."

emit_pairs() {
	for f in data/work/disasm/CODE_*.s; do
		seg=$(basename "$f" .s | sed 's/CODE_//')
		awk -v seg="$seg" '
			/^entry_jt[0-9]+:/ { match($0,/jt[0-9]+/); jt=substr($0,RSTART,RLENGTH); next }
			/^L[0-9a-f]+:/ && jt!="" { match($0,/^L[0-9a-f]+/); print seg"\t"tolower(substr($0,RSTART,RLENGTH))"\t"jt; jt=""; next }
			# JT-lea-only export: no LXXXX: label, the body starts at a raw "  addr:" line
			/^  [0-9a-f]+:/ && jt!="" { match($0,/[0-9a-f]+/); print seg"\tl"substr($0,RSTART,RLENGTH)"\t"jt; jt=""; next }
			{ if ($0 !~ /^;/ && $0 !~ /^$/) jt="" }
		' "$f"
	done | sort -t"$(printf '\t')" -k1,1n -k2,2
}

cat <<'EOF'
# lXXXX ↔ JT-export alias map

Auto-generated from `data/work/disasm/CODE_*.s` (every `entry_jtNNN:` immediately
followed by an `LXXXX:` is that JT export's local address).
Regenerate with `tools/gen_jt_aliases.sh > docs/lxxxx-jt-aliases.md`.

## WHY THIS EXISTS (read before lifting any lXXXX)

A CODE-local `lXXXX` that is ALSO a `JT[N]` export is the SAME function and is
very likely ALREADY lifted as `jtN` elsewhere in boot.c.  Before lifting an
`lXXXX` arm/helper: (1) look it up here; (2) if it maps to a `jtN`, grep boot.c
for that `jtN` — if it has a real body, ALIAS/repoint to it, don't re-lift (the
l30bc=jt882, l25ce=jt893, l2f6e duplicate traps).  NOTE: the same hex offset
recurs across CODE segments (l3540 / l2f6e / l23d2 exist in several) — these are
DIFFERENT functions; match on (CODE, offset).  When a needed name collides with
an already-lifted other-segment lXXXX, suffix `_cNN` (e.g. l23d2_c19, l3540_c19).

## Map (by CODE segment)
EOF

prev=""
emit_pairs | while IFS="$(printf '\t')" read -r seg lbl jt; do
	if [ "$seg" != "$prev" ]; then printf '\n### CODE %s\n\n' "$seg"; prev="$seg"; fi
	printf '%s = %s  ' "$lbl" "$jt"
done
printf '\n'
