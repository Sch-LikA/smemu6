#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
example_name="${1:-hello_alpha}"
example_dir="$repo_root/examples/sdcc/$example_name"
out_dir="${2:-$repo_root/tmp/sdcc-$example_name-build}"

if [[ ! -d "$example_dir" ]]; then
    echo "missing SDCC example directory: $example_dir" >&2
    exit 1
fi

mapfile -t c_sources < <(find "$example_dir" -maxdepth 1 -type f -name '*.c' | sort)
if [[ ${#c_sources[@]} -ne 1 ]]; then
    echo "expected exactly one .c file in $example_dir, found ${#c_sources[@]}" >&2
    exit 1
fi

source_path="${c_sources[0]}"
source_stem="$(basename "$source_path" .c)"
program_name="$(printf '%s' "$source_stem" | tr '[:lower:]' '[:upper:]')"

for tool in python3 sdcc sdasz80; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing required tool: $tool" >&2
        exit 1
    fi
done

rm -rf "$out_dir"
mkdir -p "$out_dir"

sdasz80 -plosgff -o "$out_dir/crt0.rel" "$example_dir/crt0.s"

sdcc -mz80 \
    -c \
    --data-loc 0x7000 \
    -o "$out_dir/$source_stem.rel" \
    "$source_path"

sdcc -mz80 \
    --no-std-crt0 \
    --code-loc 0x6000 \
    --data-loc 0x7000 \
    -o "$out_dir/$program_name.ihx" \
    "$out_dir/crt0.rel" \
    "$out_dir/$source_stem.rel"

python3 "$repo_root/tools/ihx_to_bin.py" \
    --base 0x6000 \
    "$out_dir/$program_name.ihx" \
    "$out_dir/$program_name.SM"

cat > "$out_dir/$program_name.SM.meta.json" <<EOF
{
  "date_month": 5,
  "date_year": 26,
  "entry": 24576,
  "flags": 1,
  "load": 24576,
  "type": "SM"
}
EOF

echo "wrote $out_dir/$program_name.SM"
echo "wrote $out_dir/$program_name.SM.meta.json"