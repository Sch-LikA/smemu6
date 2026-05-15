#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <smemu6-binary> <repo-root>" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root="$2"
tmp_dir="$repo_root/tmp/ctest-vfd-dx0-hostdir-boot"
stderr_log="$tmp_dir/screen.log"
stdout_log="$tmp_dir/stdout.log"

rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"
trap 'rm -rf "$tmp_dir"' EXIT

python3 "$repo_root/tools/extract_samos_image.py" \
    "$repo_root/floppies/Sys2-2.dsk" \
    "$tmp_dir/hostdir" >/dev/null

SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-launcher \
    -no-beeper \
    -floppy-hostdir "$tmp_dir/hostdir" \
    -no-display-off \
    -scrdump \
    -inject-str 'LIST\n' \
    -timeout 35 \
    > "$stdout_log" 2> "$stderr_log"

grep -F "mounted virtual host directory '$tmp_dir/hostdir' on DX0" "$stderr_log" >/dev/null
grep -F "DX0 is a writable in-memory overlay over host directory '$tmp_dir/hostdir'" "$stderr_log" >/dev/null
grep -F "SAMOS 2-2" "$stderr_log" >/dev/null
grep -F "SYS.SY" "$stderr_log" >/dev/null
grep -F "CLI.SY" "$stderr_log" >/dev/null
grep -F "Blocs libres 0209" "$stderr_log" >/dev/null
if grep -F "Erreur de lecture" "$stderr_log" >/dev/null; then
    echo "DX0 hostdir boot hit read error" >&2
    exit 1
fi