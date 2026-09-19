#!/usr/bin/env bash
set -e
cd ~/Code/C++/GateKeeper
mkdir -p benchmarks/results
BENCH=./build/gatekeeper-bench
ADDR=127.0.0.1:8080
C=50
N=50000

echo "=== Running Redis RESP2 Benchmarks ==="
echo "Running RESP2 PING..."
$BENCH -proto resp2 -addr $ADDR -op ping -c $C -n $N -out benchmarks/results/resp2_ping.json

echo "Running RESP2 RATE-LIMIT..."
$BENCH -proto resp2 -addr $ADDR -op rate-limit -c $C -n $N -out benchmarks/results/resp2_rate_limit.json

echo "Running RESP2 SET..."
$BENCH -proto resp2 -addr $ADDR -op set -c $C -n $N -out benchmarks/results/resp2_set.json

echo "Running RESP2 GET..."
$BENCH -proto resp2 -addr $ADDR -op get -c $C -n $N -out benchmarks/results/resp2_get.json

echo "=== Running Redis RESP3 Benchmarks ==="
echo "Running RESP3 PING..."
$BENCH -proto resp3 -addr $ADDR -op ping -c $C -n $N -out benchmarks/results/resp3_ping.json

echo "Running RESP3 RATE-LIMIT..."
$BENCH -proto resp3 -addr $ADDR -op rate-limit -c $C -n $N -out benchmarks/results/resp3_rate_limit.json

echo "Running RESP3 SET..."
$BENCH -proto resp3 -addr $ADDR -op set -c $C -n $N -out benchmarks/results/resp3_set.json

echo "Running RESP3 GET..."
$BENCH -proto resp3 -addr $ADDR -op get -c $C -n $N -out benchmarks/results/resp3_get.json

echo "=== RESP Benchmarks Complete! ==="
