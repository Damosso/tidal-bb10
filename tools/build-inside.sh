#!/bin/bash
# Runs inside the SDK container: compile, then package the .bar.
source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/app
(cd build && qmake -spec blackberry-armv7le-qcc CONFIG+=device ../tidal.pro && make -j"$(nproc)")
blackberry-nativepackager -package build/tidal-bb10.bar bar-descriptor.xml -configuration Device-Release
echo "packaged: build/tidal-bb10.bar"
