#!/bin/sh
# Downloads the modern-TLS QtNetwork library into lib/ (if not there yet).
#
# BB10's own QtNetwork only speaks TLS 1.0, which TIDAL's servers refuse.
# This replacement (Qt 4.8.6 QtNetwork with OpenSSL 1.1.1w built in) is made
# by JBraddockm: https://github.com/JBraddockm/bb10-qtnetwork
# It is not included in this repository because that project has no licence
# yet. The download is pinned to one commit and checked against its SHA-256.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/lib/libQtNetwork.so.4"
commit=d7bf31ee7e
sha256=3619bfc72a2d911b69f587eff9cd9878f4609bd57af1b63839e54e8b56f3191f
url="https://raw.githubusercontent.com/JBraddockm/bb10-qtnetwork/$commit/lib/libQtNetwork.so.4.8.6"

sum() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

if [ -f "$out" ] && [ "$(sum "$out")" = "$sha256" ]; then
    exit 0
fi
mkdir -p "$here/lib"
echo "Downloading libQtNetwork.so.4 from github.com/JBraddockm/bb10-qtnetwork ($commit)"
curl -sSfL -o "$out.part" "$url"
if [ "$(sum "$out.part")" != "$sha256" ]; then
    rm -f "$out.part"
    echo "Checksum mismatch: the downloaded library is not the expected one. Stopping." >&2
    exit 1
fi
mv "$out.part" "$out"
echo "OK: lib/libQtNetwork.so.4"
