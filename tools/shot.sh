#!/usr/bin/env bash
# Scripted client screenshot with up to 3 retries (the client run is flaky and may segfault at exit).
# Usage: tools/shot.sh <out.png> [build-dir=build/release] [port=7777]
# Needs the Steam client running. Not part of the gate (tools/check.sh).
set -u
out=${1:-shot.png}
dir=${2:-build/release}
port=${3:-7777}
cd "$dir" || exit 1
ext=""; [ -x ./server.exe ] && ext=".exe"

save="shot_$$.bin"
./server$ext "$port" 42 "$save" Test --local >/dev/null 2>&1 &
srv=$!
sleep 2
ok=1
for attempt in 1 2 3; do
    rm -f "$out"
    BW_SHOT="$out" BW_SHOT_FRAMES=300 timeout 60 ./client$ext --connect "127.0.0.1:$port" >/dev/null 2>&1
    if [ -s "$out" ]; then ok=0; break; fi
    echo "attempt $attempt produced no screenshot, retrying"
done
kill "$srv" 2>/dev/null
rm -f "$save"
[ $ok -eq 0 ] && echo "screenshot: $dir/$out" || echo "no screenshot after 3 attempts"
exit $ok
