#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <smemu6-binary> <repo-root>" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root="$2"
tmp_dir="$repo_root/tmp/ctest-vfd-dx1-type"
stderr_log="$tmp_dir/screen.log"
stdout_log="$tmp_dir/stdout.log"

rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"
trap 'rm -rf "$tmp_dir"' EXIT

printf 'VFDTYPEOK\n' > "$tmp_dir/TEXTFILE.BS"

SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-launcher \
    -no-beeper \
    -harddisk "$repo_root/harddisks/SM6WIN0.DSK" \
    -floppy2-hostdir "$tmp_dir" \
    -no-display-off \
    -scrdump \
    -inject-str 'TYPE DX1:TEXTFILE.BS\n' \
    -timeout 12 \
    > "$stdout_log" 2> "$stderr_log"

grep -F "mounted virtual host directory '$tmp_dir' on DX1" "$stderr_log" >/dev/null
grep -F "TYPE DX1:TEXTFILE.BS" "$stderr_log" >/dev/null
grep -F "VFDTYPEOK" "$stderr_log" >/dev/null
grep -F "fin du fichier" "$stderr_log" >/dev/null