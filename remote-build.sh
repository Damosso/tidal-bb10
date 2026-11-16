#!/bin/sh
# Builds the .bar on another machine that has Docker (x86_64), over SSH:
#   ./remote-build.sh user@server
# Copies this folder to ~/tidal-bb10-app there, runs build.sh, and copies the
# result and the build log back into build/ here. Asks for the SSH password
# only once.
set -eu
[ $# -eq 1 ] || { echo "usage: $0 user@server" >&2; exit 1; }
host=$1
here=$(cd "$(dirname "$0")" && pwd)
"$here/tools/fetch-qtnetwork.sh"
mkdir -p "$HOME/.ssh" "$here/build"
sshcmd="ssh -o ControlMaster=auto -o ControlPath=$HOME/.ssh/tidal-bb10-build-%C -o ControlPersist=120"

rsync -a --delete --exclude build --exclude .git -e "$sshcmd" "$here/" "$host:tidal-bb10-app/"
$sshcmd "$host" 'cd tidal-bb10-app && ./build.sh' || true
rsync -a -e "$sshcmd" "$host:tidal-bb10-app/build/build.log" "$here/build/" || true
rsync -a -e "$sshcmd" "$host:tidal-bb10-app/build/tidal-bb10.bar" "$here/build/" 2>/dev/null \
    && echo "Done: $here/build/tidal-bb10.bar" \
    || echo "No .bar yet - see $here/build/build.log"
