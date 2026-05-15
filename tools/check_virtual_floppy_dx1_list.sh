#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <smemu6-binary> <repo-root>" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root="$2"
tmp_dir="$repo_root/tmp/ctest-vfd-dx1-list"
stderr_log="$tmp_dir/screen.log"
stdout_log="$tmp_dir/stdout.log"

rm -rf "$tmp_dir"
mkdir -p "$tmp_dir/BOX.DR"
trap 'rm -rf "$tmp_dir"' EXIT

printf 'ROOT\n' > "$tmp_dir/ALPHA.BS"
printf 'INNER\n' > "$tmp_dir/BOX.DR/INNER.BS"

SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-launcher \
    -no-beeper \
    -harddisk "$repo_root/harddisks/SM6WIN0.DSK" \
    -floppy2-hostdir "$tmp_dir" \
    -no-display-off \
    -scrdump \
    -inject-str 'LIST DX1:\n' \
    -timeout 12 \
    > "$stdout_log" 2> "$stderr_log"

grep -F "mounted virtual host directory '$tmp_dir' on DX1" "$stderr_log" >/dev/null
grep -F "writable in-memory overlay over host directory '$tmp_dir'" "$stderr_log" >/dev/null
grep -F "ALPHA.BS" "$stderr_log" >/dev/null
grep -F "BOX.DR" "$stderr_log" >/dev/null
grep -F "Blocs libres" "$stderr_log" >/dev/null