#!/bin/sh
# ua-serve.sh DIR [PORT] - serve OpenUA's game data in DIR, read-only, to an
# Atari System V machine running /opt/openua/bin/ua-getdata. Runs in the
# foreground until Ctrl-C; nothing is installed or left running.
#
# DIR is a staged game folder, the same one the Atari builds boot from
# (`make -o frua.prg gamedata-dos DOS_DIR=... GAMEDATA_DIR=DIR` makes frua.rsc from your
# CKIT.EXE; `-o frua.prg` spares building the Atari binary). It is copied
# first, symlinks followed (rsync's daemon will not follow a link out of
# what it serves). PORT defaults to 8731.
# UA_ALLOW limits who may connect (rsyncd "hosts allow", e.g. 192.168.3.0/24).
set -e
[ $# -ge 1 ] || { echo "usage: ua-serve.sh DIR [PORT]" >&2; exit 2; }
DIR=$(cd "$1" && pwd)
PORT=${2:-8731}
[ -f "$DIR/frua.rsc" ] || echo "ua-serve.sh: warning: no frua.rsc in $DIR (the game will not start)" >&2
TMP=$(mktemp -d -t ua-serve.XXXXXX)
RS=
trap '[ -z "$RS" ] || kill $RS 2>/dev/null; rm -rf "$TMP"' EXIT
trap 'exit 130' INT TERM
mkdir "$TMP/data"
# what the TT never needs: the Atari and Unix binaries, logs, core files.
# A link with nothing behind it (frua.prg, from `make -o frua.prg
# gamedata-dos`) is skipped: rsync's exit 23
rsync -rtL --exclude=DBG.LOG --exclude=core --exclude='*.prg' --exclude='*.PRG' \
	--exclude=openua --exclude='*.o' "$DIR"/ "$TMP/data"/ 2>/dev/null || [ $? -eq 23 ]
cat > "$TMP/rsyncd.conf" <<CONF
use chroot = no
munge symlinks = no
reverse lookup = no
pid file = $TMP/rsyncd.pid
[openua]
path = $TMP/data
read only = yes
list = yes
${UA_ALLOW:+hosts allow = $UA_ALLOW}
CONF
ADDR=$(ip -4 -o addr show scope global 2>/dev/null | awk '{sub("/.*", "", $4); print $4; exit}')
echo "serving $DIR on port $PORT; on the TT: /opt/openua/bin/ua-getdata ${ADDR:-THIS-PC}${2:+:$PORT} (Ctrl-C stops)"
rsync --daemon --no-detach --config="$TMP/rsyncd.conf" --port="$PORT" --log-file=/dev/stderr &
RS=$!
wait $RS || true
RS=
