#!/usr/bin/env bash
# convert_flux.sh – Decode all KryoFlux captures in Partageables/ to flat .img files.
#
# Usage:
#   tools/convert_flux.sh [OUTDIR]
#
# OUTDIR defaults to "floppies/decoded/".  Each subdirectory of Partageables/
# is decoded with FluxEngine (smaky6 config) and written as <name>.img.
# Errors (unreadable tracks, etc.) are logged per-disk but don't stop the batch.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FLUXENGINE="${REPO_DIR}/../fluxengine/fluxengine"
PARTAGEABLES="${REPO_DIR}/Partageables"
OUTDIR="${1:-${REPO_DIR}/floppies/decoded}"

if [[ ! -x "$FLUXENGINE" ]]; then
    echo "FluxEngine binary not found at: $FLUXENGINE" >&2
    exit 1
fi

mkdir -p "$OUTDIR"

ok=0; fail=0; skip=0

while IFS= read -r -d '' dir; do
    name="$(basename "$dir")"
    # Check that there are raw KryoFlux track files inside
    if ! ls "$dir"/track00.0.raw &>/dev/null; then
        echo "  SKIP  $name  (no track00.0.raw)"
        (( skip++ )) || true
        continue
    fi

    outfile="${OUTDIR}/${name}.img"
    logfile="${OUTDIR}/${name}.log"

    # Skip if already decoded and newer than the source files
    if [[ -f "$outfile" ]] && [[ "$outfile" -nt "$dir/track00.0.raw" ]]; then
        echo "  EXIST $name"
        (( skip++ )) || true
        continue
    fi

    printf "  DECODING  %-55s → %s\n" "$name" "$(basename "$outfile")"

    if "$FLUXENGINE" read -c smaky6 \
            -s "kryoflux:${dir}" \
            -o "$outfile" \
            >"$logfile" 2>&1; then
        # Count missing sectors from log
        missing=$(grep -c 'sector.*missing\|bad sector\|error' "$logfile" 2>/dev/null || true)
        size=$(wc -c < "$outfile")
        echo "         OK  ${size} bytes  (${missing} warnings in log)"
        (( ok++ )) || true
    else
        echo "         FAILED  (see $logfile)"
        (( fail++ )) || true
    fi
done < <(find "$PARTAGEABLES" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)

echo ""
echo "Done: ${ok} decoded, ${skip} skipped, ${fail} failed"
echo "Output: ${OUTDIR}/"
