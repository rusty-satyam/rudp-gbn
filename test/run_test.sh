#!/usr/bin/env bash
# Transfers a generated test file over the reliable-UDP protocol with
# simulated packet loss on both ends, then verifies the received file
# is byte-identical to the original via checksum.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT=9199
DROP="${1:-0.1}"        # fraction of packets to randomly drop, default 10%
SIZE_KB="${2:-256}"     # test file size in KB, default 256KB

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

IN="$WORKDIR/input.bin"
OUTDIR="$WORKDIR/out"
mkdir -p "$OUTDIR"

head -c $((SIZE_KB * 1024)) /dev/urandom > "$IN"

"$ROOT/bin/server" -p "$PORT" -d "$DROP" -o "$OUTDIR" &
SERVER_PID=$!
sleep 0.3

"$ROOT/bin/client" -h 127.0.0.1 -p "$PORT" -d "$DROP" -f "$IN"

wait "$SERVER_PID"

SUM_IN=$(sha256sum "$IN" | awk '{print $1}')
SUM_OUT=$(sha256sum "$OUTDIR/input.bin" | awk '{print $1}')

echo
echo "input  sha256: $SUM_IN"
echo "output sha256: $SUM_OUT"

if [ "$SUM_IN" = "$SUM_OUT" ]; then
    echo "PASS: file transferred correctly under ${DROP} simulated loss rate"
else
    echo "FAIL: checksums differ"
    exit 1
fi
