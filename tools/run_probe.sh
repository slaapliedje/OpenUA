#!/usr/bin/env bash
#
# Boot frua.prg in Hatari, fast-forward through a configurable
# number of seconds, capture the engine probe trace, then force-
# kill Hatari cleanly. Used during bring-up to inspect which
# stubs / arms fire in the boot path.
#
# Hatari catches SIGTERM and pops a "Really quit?" confirmation
# dialog, leaving the process running. We bypass that by sending
# SIGKILL directly via `timeout -s KILL`. SIGKILL can't be caught,
# so Hatari dies cleanly even if it's in the middle of a frame
# or holding a modal.
#
# Usage:
#   tools/run_probe.sh [duration_s] [output_log]
#
# Defaults: 15 seconds, /tmp/probe.log
#
# Environment:
#   FALCON_TOS   path to Falcon TOS 4.0x ROM (default /usr/share/hatari/TOSv4.04.img)
#   HATARI_ARGS  extra args appended to the Hatari command line
#
# Requires:
#   - hatari on PATH
#   - frua.prg built (run `make ENGINE_PROBE=1` first to capture
#     the probe trace; otherwise the log will only show TOS chatter)

set -euo pipefail

DURATION="${1:-15}"
OUT_LOG="${2:-/tmp/probe.log}"
FALCON_TOS="${FALCON_TOS:-/usr/share/hatari/TOSv4.04.img}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# GEMDOS mount dir: defaults to the repo root, but can point at a staged
# game-data folder (e.g. data/work/gamedata) so the engine can open real
# .DAT/.GLB/.CTL files by bare filename. Override: GEMDOS_DIR=path make probe
GEMDOS_DIR="${GEMDOS_DIR:-$REPO}"

if [[ ! -f frua.prg ]]; then
	echo "run_probe.sh: frua.prg not found; run 'make ENGINE_PROBE=1' first" >&2
	exit 1
fi

if [[ ! -f "$FALCON_TOS" ]]; then
	echo "run_probe.sh: $FALCON_TOS not found; set FALCON_TOS to your Falcon TOS image" >&2
	exit 1
fi

# Kill any stale Hatari instances from earlier runs that may have
# been left in the "Really quit?" dialog. Best-effort; ignore
# "no such process" failures.
pkill -9 -f 'hatari .*--auto.*frua.prg' 2>/dev/null || true

# --conout 2 mirrors VT-52 console output to stdout (where our
# engine probes land via dbg_log). --fast-forward runs Hatari at
# max wall-clock speed so a 30 L2d3e iteration boot finishes
# inside the configured duration.
#
# `timeout -s KILL` sends SIGKILL after $DURATION, bypassing
# Hatari's quit-confirmation dialog. SIGKILL is uncatchable, so
# the process dies cleanly even if it's holding a modal.
# timeout exits 124 (graceful) or 137 (SIGKILL); the child also
# gets reported by bash as "Killed" on stderr. Running through a
# subshell with `wait` swallows both — we want a clean log and
# don't care about the exit status (we always SIGKILL).
{
	timeout -s KILL "${DURATION}s" \
		hatari \
			--machine falcon \
			--fast-forward yes \
			--dsp emu \
			--tos "$FALCON_TOS" \
			--conout 2 \
			-d "$GEMDOS_DIR" \
			--auto 'C:\frua.prg' \
			${HATARI_ARGS:-} \
		> "$OUT_LOG" 2>&1 &
	PID=$!
	wait "$PID" 2>/dev/null || true
} 2>/dev/null

# Final sanity: make sure no stray Hatari is still up.
pkill -9 -f 'hatari .*--auto.*frua.prg' 2>/dev/null || true

# Surface the line count so callers can tell instantly if Hatari
# never produced output.
LINES="$(wc -l < "$OUT_LOG")"
echo "run_probe.sh: $LINES lines captured to $OUT_LOG"
