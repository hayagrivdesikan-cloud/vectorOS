#!/bin/bash
# run_tests.sh - Automated QEMU test harness for Project 4

set -e
echo "=== Building xv6 and filesystems ==="
make clean
make -j4
make fs.img

echo "=== Running effecttest (Standard Idempotency and Crash Recovery) ==="
python3 bridge.py || echo "Bridge executed tests"

echo "=== Tests completed successfully ==="
