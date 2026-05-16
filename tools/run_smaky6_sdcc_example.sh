#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <smemu6-binary> [work-dir]" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root=$(cd "$(dirname "$0")/.." && pwd)
work_dir="${2:-$repo_root/tmp/sdcc-hello}"
build_dir="$work_dir/build"
hostdir="$work_dir/hostdir"
stdout_log="$work_dir/stdout.log"
stderr_log="$work_dir/stderr.log"

rm -rf "$work_dir"
mkdir -p "$work_dir"

"$repo_root/tools/build_smaky6_sdcc_example.sh" "$build_dir"

python3 "$repo_root/tools/extract_samos_image.py" \
    "$repo_root/floppies/Sys2-2.dsk" \
    "$hostdir" >/dev/null

cp "$build_dir/HELLO.SM" "$hostdir/"
cp "$build_dir/HELLO.SM.meta.json" "$hostdir/"

SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-beeper \
    -no-launcher \
    -floppy-hostdir "$hostdir" \
    -no-display-off \
    -scrdump \
    -inject-str "HELLO\n" \
    -timeout 35 \
    > "$stdout_log" 2> "$stderr_log"

echo "logs: $stdout_log $stderr_log"
echo "hostdir: $hostdir"