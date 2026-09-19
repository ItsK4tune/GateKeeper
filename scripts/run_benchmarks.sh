#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== GateKeeper Automated Benchmark Runner ==="
cd "${ROOT_DIR}"

if [ ! -f "build/gatekeeper" ]; then
    echo "Building GateKeeper in Release mode..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc)
fi

echo "Starting GateKeeper daemon (in-memory, log=none)..."
./build/gatekeeper --port 63779 --http-port 8080 --timer 50 --log none &
SERVER_PID=$!

cleanup() {
    echo "Stopping GateKeeper daemon (PID: ${SERVER_PID})..."
    kill -9 "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1

echo ""
echo ">>> [1/8] GKWP/1 - PING (Baseline I/O)"
go run tools/bench/main.go -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 100000 -op ping

echo ""
echo ">>> [2/8] GKWP/1 - Rate Limit (Sliding Window Counter - Hybrid)"
go run tools/bench/main.go -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 100000 -op rate-limit

echo ""
echo ">>> [3/8] GKWP/1 - Key-Value SET (Write)"
go run tools/bench/main.go -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 100000 -op set

echo ""
echo ">>> [4/8] GKWP/1 - Key-Value GET (Read)"
go run tools/bench/main.go -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 100000 -op get

echo ""
echo ">>> [5/8] HTTP/1.1 - Health Check (Baseline)"
go run tools/bench/main.go -proto http -addr 127.0.0.1:8080 -c 50 -n 50000 -op ping

echo ""
echo ">>> [6/8] HTTP/1.1 - Rate Limit Check"
go run tools/bench/main.go -proto http -addr 127.0.0.1:8080 -c 50 -n 50000 -op rate-limit

echo ""
echo ">>> [7/8] HTTP/1.1 - Key-Value SET"
go run tools/bench/main.go -proto http -addr 127.0.0.1:8080 -c 50 -n 50000 -op set

echo ""
echo ">>> [8/8] HTTP/1.1 - Key-Value GET"
go run tools/bench/main.go -proto http -addr 127.0.0.1:8080 -c 50 -n 50000 -op get

echo ""
echo "=== All Benchmarks Completed Successfully! ==="
