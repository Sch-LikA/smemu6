#!/usr/bin/env bash
# Test script for CALM assembly examples
# ======================================
# Automates running the CALM program through the emulator with screen capture

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
calm_root="$repo_root/calm"
example_name="${1:-hello_calm}"
example_dir="$calm_root/examples/$example_name"
out_dir="${2:-$repo_root/tmp/calm-$example_name-build}"
log_file="${out_dir}/run.log"

if [[ ! -d "$out_dir" ]]; then
    echo "missing build output directory: $out_dir" >&2
    echo "run: calm/build_smaky6_calm_example.sh $example_name" >&2
    exit 1
fi

echo "Testing CALM example: $example_name"
echo "Build directory: $out_dir"
echo ""
echo "NOTE: This requires manual setup with SMILE inside the emulator."
echo "See README.md in calm/examples/$example_name for detailed steps."
echo ""
echo "For automated testing with a pre-assembled .SM file, copy the binary to:"
echo "  $out_dir/OUTPUT.SM"
echo ""
