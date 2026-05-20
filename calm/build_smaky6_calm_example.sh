#!/usr/bin/env bash
# Prepare CALM assembly example files
# ===================================
# This script simply copies CALM source files to a build directory.
# Assembly must be done manually using SMILE inside the emulator.

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
calm_root="$repo_root/calm"
example_name="${1:-hello_calm}"
example_dir="$calm_root/examples/$example_name"
out_dir="${2:-$repo_root/tmp/calm-$example_name-build}"

if [[ ! -d "$example_dir" ]]; then
    echo "missing CALM example directory: $example_dir" >&2
    exit 1
fi

# Find the .SR source file
mapfile -t sr_sources < <(find "$example_dir" -maxdepth 1 -type f -name '*.SR' | sort)
if [[ ${#sr_sources[@]} -ne 1 ]]; then
    echo "expected exactly one .SR file in $example_dir, found ${#sr_sources[@]}" >&2
    exit 1
fi

source_path="${sr_sources[0]}"
source_stem="$(basename "$source_path" .SR)"
program_name="$(printf '%s' "$source_stem" | tr '[:lower:]' '[:upper:]')"

echo "Preparing CALM example: $example_name"
echo "  Source: $source_path"
echo "  Build output: $out_dir"

rm -rf "$out_dir"
mkdir -p "$out_dir"

# Copy the source file to output
cp "$source_path" "$out_dir/$source_stem.SR"

# Copy the example README
if [[ -f "$example_dir/README.md" ]]; then
    cp "$example_dir/README.md" "$out_dir/EXAMPLE_README.md"
fi

echo "Done. Files ready in: $out_dir"
