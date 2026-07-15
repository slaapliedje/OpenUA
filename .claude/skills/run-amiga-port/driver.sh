#!/usr/bin/env bash
# run-amiga-port driver: build frua (AmigaOS hunk), boot it in amiberry on an
# emulated A1200, screenshot, send keys/clicks, check sound. The Amiga twin of
# .claude/skills/run-falcon-port/driver.sh.
#
# amiberry-specific realities this driver encodes (learned live):
#  - The flatpak opens its window on the DESKTOP display (:0), not an Xvfb.
#  - The emulated mouse is DELTA-driven (JOY0DAT): the first in-window click
#    captures the host pointer; after that only RELATIVE host moves reach the
#    game. `start` does the capture click at the screen centre (a dead zone on
#    the main menu) and the driver TRACKS the emulated position from there.
#  - The game samples the mouse button from CIA PRA at 50 Hz: an instantaneous
#    synthetic click is INVISIBLE. `click` holds the button 0.3 s.
#  - Keys: ONE per xdotool invocation, window re-activated first; back-to-back
#    keys in one call get lost.
#  - The engine's boot/debug trail is PROGDIR:DBG.LOG in the mounted dir.
set -u

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
MOUNT="${FRUA_AMIGA_MOUNT:-$REPO/data/work/amiga-mount}"
CONF="${AMIBERRY_CONF:-$HOME/Amiberry/Configurations/openua.uae}"
STATE="${FRUA_AMIGA_STATE:-/tmp/frua-amiga}"
DBGLOG="$MOUNT/DBG.LOG"
DISP="${FRUA_AMIGA_DISPLAY:-:0}"
LOGFILE="$STATE/amiberry.log"

mkdir -p "$STATE"

xd() { DISPLAY="$DISP" xdotool "$@"; }

find_window() {
	DISPLAY="$DISP" xwininfo -root -tree 2>/dev/null \
	    | grep -io '0x[0-9a-f]* "Amiberry' | head -1 | cut -d' ' -f1
}

window_id() {
	local w
	w=$(cat "$STATE/window" 2>/dev/null)
	if [ -z "$w" ] || ! DISPLAY="$DISP" xwininfo -id "$w" >/dev/null 2>&1; then
		w=$(find_window)
		[ -n "$w" ] && echo "$w" > "$STATE/window"
	fi
	echo "$w"
}

# Tracked emulated-mouse position (lores 320x200 coords). Known-good right
# after `start` (the pointer boots at the centre); every `move`/`click x y`
# updates it. Host-relative deltas map 1:1 to lores pixels in this config.
pos_get() { cat "$STATE/mousepos" 2>/dev/null || echo "160 100"; }
pos_set() { echo "$1 $2" > "$STATE/mousepos"; }

cmd_build() {
	( cd "$REPO" && make MACHINE=amiga ) || return 1
	cp "$REPO/frua" "$MOUNT/frua"
	echo "staged: $MOUNT/frua"
}

cmd_start() {
	local w tries=0
	pkill -x amiberry 2>/dev/null
	rm -f "$DBGLOG"
	( flatpak run --env=SDL_VIDEODRIVER=x11 com.blitterstudio.amiberry \
	    --log --config "$CONF" -G >"$LOGFILE" 2>&1 & )
	# Boot is done when the engine's modal is up (~40 s emulated boot).
	until grep -q "menu: modal up" "$DBGLOG" 2>/dev/null; do
		sleep 3
		tries=$((tries + 1))
		if [ $tries -gt 60 ]; then
			echo "boot timed out (no 'menu: modal up' in $DBGLOG)" >&2
			tail -5 "$LOGFILE" >&2
			return 1
		fi
	done
	w=$(find_window)
	if [ -z "$w" ]; then
		echo "no amiberry window on $DISP" >&2
		return 1
	fi
	echo "$w" > "$STATE/window"
	# Capture the mouse: one click at the window centre (the main menu's dead
	# zone). After this, relative moves drive the emulated pointer, which
	# boots at lores (160,100).
	local geo x y wpx hpx
	geo=$(DISPLAY="$DISP" xwininfo -id "$w" | awk '
		/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4}
		/Width/ {w=$2} /Height/ {h=$2}
		END {print x, y, w, h}')
	read -r x y wpx hpx <<< "$geo"
	xd windowactivate --sync "$w" mousemove $((x + wpx / 2)) $((y + hpx / 2)) click 1
	pos_set 160 100
	echo "up: window $w, menu modal, mouse captured at (160,100)"
}

cmd_stop() { pkill -x amiberry 2>/dev/null; echo stopped; }

cmd_shot() {
	local out="${1:?usage: shot <png>}" w
	w=$(window_id)
	[ -z "$w" ] && { echo "no window" >&2; return 1; }
	DISPLAY="$DISP" import -window "$w" "$out"
	echo "$out"
}

cmd_key() {
	local w
	w=$(window_id)
	[ -z "$w" ] && { echo "no window" >&2; return 1; }
	for k in "$@"; do
		xd windowactivate --sync "$w" key --clearmodifiers "$k"
		sleep 0.4
	done
}

cmd_move() {
	local dx="${1:?usage: move <dx> <dy>}" dy="${2:?}" px py
	read -r px py <<< "$(pos_get)"
	xd mousemove_relative -- "$dx" "$dy"
	pos_set $((px + dx)) $((py + dy))
	sleep 0.3
}

cmd_click() {
	local w px py
	w=$(window_id)
	[ -z "$w" ] && { echo "no window" >&2; return 1; }
	if [ $# -ge 2 ]; then
		# Target lores coords: move there from the tracked position first.
		read -r px py <<< "$(pos_get)"
		xd mousemove_relative -- $(($1 - px)) $(($2 - py))
		pos_set "$1" "$2"
		sleep 0.3
	fi
	xd mousedown 1; sleep 0.3; xd mouseup 1
	sleep 0.3
}

cmd_wait() {
	local re="${1:?usage: wait <regex> [count]}" n="${2:-1}" tries=0
	until [ "$(grep -cE "$re" "$DBGLOG" 2>/dev/null)" -ge "$n" ]; do
		sleep 2
		tries=$((tries + 1))
		[ $tries -gt 90 ] && { echo "wait '$re' timed out" >&2; return 1; }
	done
}

cmd_log() { cat "$DBGLOG" 2>/dev/null; }

cmd_smoke() {
	local out="${1:-/tmp/frua-amiga-menu.png}"
	cmd_build && cmd_start && cmd_shot "$out" && cmd_stop
}

cmd_sound() {
	# Rebuild with the machine-neutral boot sound harness (4 sfx + song 0),
	# boot, capture the HOST audio from the default sink's monitor, and check
	# it is not silence. Requires amiberry's config sound_output != none.
	local out="${1:-/tmp/frua-amiga-sound.wav}" sink
	if grep -q "sound_output=none" "$CONF"; then
		echo "amiberry config has sound_output=none — no capture possible" >&2
		return 1
	fi
	( cd "$REPO" && make MACHINE=amiga EXTRA_CFLAGS=-DFRUA_SNDTEST ) || return 1
	cp "$REPO/frua" "$MOUNT/frua"
	pkill -x amiberry 2>/dev/null
	rm -f "$DBGLOG"
	( flatpak run --env=SDL_VIDEODRIVER=x11 com.blitterstudio.amiberry \
	    --log --config "$CONF" -G >"$LOGFILE" 2>&1 & )
	sleep 6
	sink=$(pactl get-default-sink)
	parec -d "$sink.monitor" --file-format=wav "$out" &
	local rec=$!
	sleep 55
	kill $rec 2>/dev/null
	pkill -x amiberry 2>/dev/null
	# Restore the normal binary so a later `start` isn't the test build.
	( cd "$REPO" && make MACHINE=amiga ) && cp "$REPO/frua" "$MOUNT/frua"
	python3 - "$out" <<'EOF'
import sys, wave, array
w = wave.open(sys.argv[1]); n, ch, fr = w.getnframes(), w.getnchannels(), w.getframerate()
a = array.array('h'); a.frombytes(w.readframes(n))
loud = 0
for s in range(n // fr):
    seg = a[s*fr*ch:(s+1)*fr*ch]
    if seg and (sum(x*x for x in seg)/len(seg))**0.5 > 200: loud += 1
print(f"{n/fr:.0f}s captured, {loud}s loud")
sys.exit(0 if loud >= 5 else 1)
EOF
}

case "${1:-}" in
	build) shift; cmd_build "$@" ;;
	start) shift; cmd_start "$@" ;;
	stop)  shift; cmd_stop  "$@" ;;
	shot)  shift; cmd_shot  "$@" ;;
	key)   shift; cmd_key   "$@" ;;
	move)  shift; cmd_move  "$@" ;;
	click) shift; cmd_click "$@" ;;
	wait)  shift; cmd_wait  "$@" ;;
	log)   shift; cmd_log   "$@" ;;
	smoke) shift; cmd_smoke "$@" ;;
	sound) shift; cmd_sound "$@" ;;
	*) echo "usage: $(basename "$0") build | start | stop | shot <png> | key <keysym>... | move <dx> <dy> | click [x y] | wait <regex> [n] | log | smoke [png] | sound [wav]" >&2; exit 2 ;;
esac
