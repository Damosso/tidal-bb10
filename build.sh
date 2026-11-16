#!/bin/sh
# Builds build/tidal-bb10.bar inside a Docker image that contains the
# BlackBerry 10 Native SDK 10.3.1. The image is a community one from Docker Hub
# (override with BB10_NDK_IMAGE). Output is also saved to build/build.log.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
image=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
"$here/tools/fetch-qtnetwork.sh"
mkdir -p "$here/build"
docker run --rm -v "$here:/src" -w /src "$image" \
    bash tools/build-inside.sh 2>&1 | tee "$here/build/build.log"
