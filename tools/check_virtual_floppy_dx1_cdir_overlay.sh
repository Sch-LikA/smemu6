#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <smemu6-binary> <repo-root>" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root="$2"
tmp_dir="$repo_root/tmp/ctest-vfd-dx1-cdir-overlay"
stderr_log="$tmp_dir/screen.log"
stdout_log="$tmp_dir/stdout.log"

rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"
trap 'rm -rf "$tmp_dir"' EXIT

printf 'ROOT\n' > "$tmp_dir/ALPHA.BS"

SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-launcher \
    -no-beeper \
    -harddisk "$repo_root/harddisks/SM6WIN0.DSK" \
    -floppy2-hostdir "$tmp_dir" \
    -no-display-off \
    -scrdump \
    -inject-str 'CDIR DX1:NEWBOX\n\fLIST DX1:\n' \
    -timeout 35 \
    > "$stdout_log" 2> "$stderr_log"

grep -F "mounted virtual host directory '$tmp_dir' on DX1" "$stderr_log" >/dev/null
grep -F "writable in-memory overlay over host directory '$tmp_dir'" "$stderr_log" >/dev/null
grep -F "CDIR DX1:NEWBOX" "$stderr_log" >/dev/null
grep -F "LIST DX1:" "$stderr_log" >/dev/null
grep -F "NEWBOX" "$stderr_log" >/dev/null
if [[ -e "$tmp_dir/NEWBOX.DR" ]]; then
    echo "unexpected NEWBOX.DR created in host directory; overlay must stay in memory" >&2
    exit 1
fi