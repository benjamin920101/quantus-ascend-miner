#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
cd "$ROOT/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
echo "Binary: $ROOT/build/quantus_ascend_miner"
echo "Run:"
echo "  ./quantus_ascend_miner --pool stratum+tcp://qtc.kryptex.network:7049 --wallet qzYourAddress --worker test"
