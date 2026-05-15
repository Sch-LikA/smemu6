#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <smemu6-binary> <repo-root>" >&2
    exit 2
fi

smemu6_bin="$1"
repo_root="$2"
tmp_image="$repo_root/tmp/ctest-win-overlay-delete.dsk"
stderr_log="$repo_root/tmp/ctest-win-overlay-delete.log"
stdout_log="$repo_root/tmp/ctest-win-overlay-delete.stdout"

rm -f "$tmp_image" "$stderr_log" "$stdout_log"
trap 'rm -f "$tmp_image" "$stderr_log" "$stdout_log"' EXIT

cp "$repo_root/harddisks/SM6WIN0.DSK" "$tmp_image"
before_hash=$(sha256sum "$tmp_image" | awk '{print $1}')

SDL_VIDEODRIVER=dummy "$smemu6_bin" \
    -no-launcher \
    -no-beeper \
    -harddisk "$tmp_image" \
    -no-display-off \
    -scrdump \
    -inject-str 'DELETE BIORY.BS\n\fLIST\n' \
    -timeout 45 \
    > "$stdout_log" 2> "$stderr_log"

after_hash=$(sha256sum "$tmp_image" | awk '{print $1}')

grep -F "[win] drive 0 mounted: $tmp_image" "$stderr_log" >/dev/null
grep -F "DELETE BIORY.BS" "$stderr_log" >/dev/null
grep -F "Blocs libres 060408" "$stderr_log" >/dev/null

if [[ "$before_hash" != "$after_hash" ]]; then
    echo "backing hard-disk image changed despite overlay semantics" >&2
    exit 1
fi

if awk '
    /\* LIST-/ { in_list = 1; next }
    in_list && /BIORY\.BS/ { found = 1 }
    END { exit found ? 0 : 1 }
' "$stderr_log"; then
    echo "BIORY.BS still appears after overlay delete" >&2
    exit 1
fi