#!/usr/bin/env bash
#
# dosdrive.sh — drive SSI's MS-DOS FRUA 1.2 headless, so it can arbitrate
# "does the original do X?" questions. See docs/dos-reference.md for the
# DOSBox setup; this is the input half.
#
#   tools/dosdrive.sh boot                 launch + pass title/copy-protection
#   tools/dosdrive.sh shot out.png         screenshot the DOSBox window
#   tools/dosdrive.sh cursor               print the cursor's image coords
#   tools/dosdrive.sh click <x> <y>        move there and click
#   tools/dosdrive.sh key Return           one keystroke
#   tools/dosdrive.sh type NAME            type a string
#   tools/dosdrive.sh stop
#
# ★ FRUA'S DOS MENUS ARE MOUSE-ONLY. The letter accelerators that drive the
#   Mac/port builds (p / l / e ...) do NOTHING here — verified by pressing them
#   at the Hall with no effect, then clicking the same control and having it
#   open. Every menu step below has to be a click.
#
# ★ THE CAPTURED MOUSE IS DELTA-DRIVEN *AND ACCELERATED*. The host->screen
#   ratio measured 0.92/0.78 on one move and 0.99/0.88 on the very next, so a
#   computed jump does not land where the arithmetic says. `click` therefore
#   CLOSES THE LOOP: locate the cursor, move, look again, repeat. Do not
#   "optimise" it back into a single mousemove_relative.
#
# ★ THE CURSOR IS FOUND BY ITS YELLOW. Both cursor shapes (blue shield in
#   menus, sword in play) contain bright yellow 255,255,85 and little else
#   does. Bright CYAN is NOT usable — the title screen paints 6685 pixels of
#   it. The locator takes the largest yellow blob so a stray yellow glyph
#   cannot win.
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUN="${FRUA_DOS_DIR:-$REPO/data/work/dos-run}"
DISP="${FRUA_DOS_DISPLAY:-:99}"
STATE="${FRUA_DOS_STATE:-/tmp/frua-dos}"
mkdir -p "$STATE"
export DISPLAY="$DISP"

win() {
	xwininfo -root -tree 2>/dev/null \
	    | grep -iE '"[^"]*dosbox[^"]*"' | head -1 | grep -oE '0x[0-9a-f]+'
}

cmd_shot() { import -window "$(win)" "${1:?usage: shot <png>}"; }

cmd_cursor() {
	import -window "$(win)" "$STATE/_c.png"
	python3 - "$STATE/_c.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load(); w, h = im.size
pts = {(x, y) for y in range(h) for x in range(w) if px[x, y] == (255, 255, 85)}
best = []
while pts:
    seed = pts.pop(); blob = [seed]; stack = [seed]
    while stack:
        x, y = stack.pop()
        for dx in range(-3, 4):
            for dy in range(-3, 4):
                q = (x + dx, y + dy)
                if q in pts:
                    pts.discard(q); blob.append(q); stack.append(q)
    if len(blob) > len(best): best = blob
if best:
    print(sum(p[0] for p in best) // len(best), sum(p[1] for p in best) // len(best))
PY
}

cmd_moveto() {
	local tx="${1:?}" ty="${2:?}" i cx cy dx dy
	for i in 1 2 3 4 5 6 7 8; do
		read -r cx cy <<< "$(cmd_cursor)"
		[ -z "${cx:-}" ] && { echo "moveto: cursor not found" >&2; return 1; }
		dx=$((tx - cx)); dy=$((ty - cy))
		[ ${dx#-} -le 6 ] && [ ${dy#-} -le 6 ] && return 0
		# ~90% of the naive step: acceleration makes overshoot the common
		# failure and an undershoot only costs another iteration
		xdotool mousemove_relative -- $((dx * 10 / 11)) $((dy * 11 / 10))
		sleep 0.4
	done
}

cmd_click() {
	[ $# -ge 2 ] && cmd_moveto "$1" "$2"
	xdotool mousedown 1; sleep 0.35; xdotool mouseup 1; sleep "${3:-3}"
}

cmd_key()  { xdotool windowfocus --sync "$(win)" 2>/dev/null
             xdotool key --clearmodifiers "${1:?}"; sleep "${2:-2}"; }
cmd_type() { xdotool windowfocus --sync "$(win)" 2>/dev/null
             xdotool type --delay 120 "${1:?}"; sleep 1; }

cmd_boot() {
	pkill -f dosbox 2>/dev/null; sleep 1
	# ★ The flatpak forces DISPLAY=:0 and bind-mounts only that socket, so the
	# display has to be set INSIDE the sandbox and reached over TCP — the Xvfb
	# needs `-listen tcp -ac`. See docs/dos-reference.md.
	nohup flatpak run --command=/bin/sh io.github.dosbox-staging -c \
	    "DISPLAY=127.0.0.1:${DISP#:} exec /app/bin/dosbox -conf $RUN/frua.conf" \
	    > "$STATE/dosbox.log" 2>&1 &
	sleep 25
	local w; w=$(win)
	[ -z "$w" ] && { echo "no DOSBox window on $DISP" >&2; return 1; }
	cmd_key Return 3; cmd_key Return 3            # title
	local c; for c in x x x; do cmd_key $c 1; done # copy protection (hacked)
	cmd_key Return 5
	# capture the mouse: one click in the window
	local geo x y ww hh
	geo=$(xwininfo -id "$w" | awk '
		/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4}
		/Width/ {a=$2} /Height/ {b=$2} END {print x, y, a, b}')
	read -r x y ww hh <<< "$geo"
	xdotool windowfocus --sync "$w"
	xdotool mousemove $((x + ww / 2)) $((y + hh / 2)); sleep 1
	xdotool mousedown 1; sleep 0.4; xdotool mouseup 1; sleep 3
	echo "up: window $w, main menu, mouse captured"
}

cmd_stop() { pkill -f dosbox 2>/dev/null; echo stopped; }

case "${1:-}" in
boot)   shift; cmd_boot   "$@" ;;
shot)   shift; cmd_shot   "$@" ;;
cursor) shift; cmd_cursor "$@" ;;
moveto) shift; cmd_moveto "$@" ;;
click)  shift; cmd_click  "$@" ;;
key)    shift; cmd_key    "$@" ;;
type)   shift; cmd_type   "$@" ;;
stop)   shift; cmd_stop   "$@" ;;
*) echo "usage: $(basename "$0") boot|shot <png>|cursor|click <x> <y>|key <k>|type <s>|stop" >&2; exit 2 ;;
esac
