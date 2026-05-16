#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "usage: $0 <smemu6-binary> [example-name] [work-dir]" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root=$(cd "$(dirname "$0")/.." && pwd)
sdcc_root="$repo_root/sdcc"
example_name="${2:-hello_alpha}"
example_dir="$sdcc_root/examples/$example_name"
work_dir="${3:-$repo_root/tmp/sdcc-$example_name}"
build_dir="$work_dir/build"
hostdir="$work_dir/hostdir"
stdout_log="$work_dir/stdout.log"
stderr_log="$work_dir/stderr.log"

if [[ ! -d "$example_dir" ]]; then
    echo "missing SDCC example directory: $example_dir" >&2
    exit 1
fi

mapfile -t c_sources < <(find "$example_dir" -maxdepth 1 -type f -name '*.c' | sort)
if [[ ${#c_sources[@]} -ne 1 ]]; then
    echo "expected exactly one .c file in $example_dir, found ${#c_sources[@]}" >&2
    exit 1
fi

source_stem="$(basename "${c_sources[0]}" .c)"
program_name="$(printf '%s' "$source_stem" | tr '[:lower:]' '[:upper:]')"

rm -rf "$work_dir"
mkdir -p "$work_dir"

"$sdcc_root/build_smaky6_sdcc_example.sh" "$example_name" "$build_dir"

python3 "$repo_root/tools/extract_samos_image.py" \
    "$repo_root/floppies/Sys2-2.dsk" \
    "$hostdir" >/dev/null

cp "$build_dir/$program_name.SM" "$hostdir/"
cp "$build_dir/$program_name.SM.meta.json" "$hostdir/"

SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-beeper \
    -no-launcher \
    -floppy-hostdir "$hostdir" \
    -no-display-off \
    -scrdump \
    -inject-str "$program_name\n" \
    -timeout 35 \
    > "$stdout_log" 2> "$stderr_log"

echo "logs: $stdout_log $stderr_log"
echo "hostdir: $hostdir"