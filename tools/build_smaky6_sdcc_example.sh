#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
example_dir="$repo_root/examples/sdcc/hello_alpha"
out_dir="${1:-$repo_root/tmp/sdcc-hello-build}"

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
    -o "$out_dir/hello.rel" \
    "$example_dir/hello.c"

sdcc -mz80 \
    --no-std-crt0 \
    --code-loc 0x6000 \
    --data-loc 0x7000 \
    -o "$out_dir/HELLO.ihx" \
    "$out_dir/crt0.rel" \
    "$out_dir/hello.rel"

python3 "$repo_root/tools/ihx_to_bin.py" \
    --base 0x6000 \
    "$out_dir/HELLO.ihx" \
    "$out_dir/HELLO.SM"

cat > "$out_dir/HELLO.SM.meta.json" <<'EOF'
{
  "date_month": 5,
  "date_year": 26,
  "entry": 24576,
  "flags": 1,
  "load": 24576,
  "type": "SM"
}
EOF

echo "wrote $out_dir/HELLO.SM"
echo "wrote $out_dir/HELLO.SM.meta.json"