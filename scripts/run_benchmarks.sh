#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== GateKeeper Universal Benchmark Runner ==="
cd "${ROOT_DIR}"

RESULTS_DIR="${ROOT_DIR}/benchmarks/results"
mkdir -p "${RESULTS_DIR}"

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

cd "${ROOT_DIR}/tools/bench"

echo ""
echo ">>> [1/8] GKWP/1 - PING"
go run . -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 50000 -op ping -out "${RESULTS_DIR}/gkwp1_ping.json"

echo ""
echo ">>> [2/8] GKWP/1 - Rate Limit (Sliding Window Counter - Hybrid)"
go run . -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 50000 -op rate-limit -out "${RESULTS_DIR}/gkwp1_ratelimit.json"

echo ""
echo ">>> [3/8] GKWP/1 - Key-Value SET (Write)"
go run . -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 50000 -op set -out "${RESULTS_DIR}/gkwp1_set.json"

echo ""
echo ">>> [4/8] GKWP/1 - Key-Value GET (Read)"
go run . -proto gkwp1 -addr 127.0.0.1:63779 -c 50 -n 50000 -op get -out "${RESULTS_DIR}/gkwp1_get.json"

echo ""
echo ">>> [5/8] HTTP/1.1 - Health Check"
go run . -proto http1 -addr 127.0.0.1:8080 -c 50 -n 20000 -op ping -out "${RESULTS_DIR}/http1_ping.json"

echo ""
echo ">>> [6/8] HTTP/1.1 - Rate Limit Check"
go run . -proto http1 -addr 127.0.0.1:8080 -c 50 -n 20000 -op rate-limit -out "${RESULTS_DIR}/http1_ratelimit.json"

echo ""
echo ">>> [7/8] HTTP/1.1 - Key-Value SET"
go run . -proto http1 -addr 127.0.0.1:8080 -c 50 -n 20000 -op set -out "${RESULTS_DIR}/http1_set.json"

echo ""
echo ">>> [8/8] HTTP/1.1 - Key-Value GET"
go run . -proto http1 -addr 127.0.0.1:8080 -c 50 -n 20000 -op get -out "${RESULTS_DIR}/http1_get.json"

echo ""
echo "=== All Baseline Benchmarks Completed Successfully! Results saved to ${RESULTS_DIR} ==="
