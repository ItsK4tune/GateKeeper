#!/usr/bin/env bash
set -e
cd ~/Code/C++/GateKeeper
mkdir -p benchmarks/results
BENCH=./build/gatekeeper-bench
ADDR=127.0.0.1:8080
C=50
N=50000

echo "Running GKWP/2 PING..."
$BENCH -proto gkwp2 -addr $ADDR -op ping -c $C -n $N -out benchmarks/results/gkwp2_ping.json

echo "Running GKWP/2 RATE-LIMIT..."
$BENCH -proto gkwp2 -addr $ADDR -op rate-limit -c $C -n $N -out benchmarks/results/gkwp2_rate_limit.json

echo "Running GKWP/2 SET..."
$BENCH -proto gkwp2 -addr $ADDR -op set -c $C -n $N -out benchmarks/results/gkwp2_set.json

echo "Running GKWP/2 GET..."
$BENCH -proto gkwp2 -addr $ADDR -op get -c $C -n $N -out benchmarks/results/gkwp2_get.json

echo "GKWP/2 Benchmarks complete!"
