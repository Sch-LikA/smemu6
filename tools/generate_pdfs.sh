#!/usr/bin/env bash
# generate_pdfs.sh — Regenerate all Smaky 6 manual PDFs from Markdown sources.
#
# Usage:
#   ./tools/generate_pdfs.sh [--out DIR]
#
# Requirements: pandoc, lualatex (texlive-luatex + texlive-latex-extra +
#               texlive-fonts-recommended + texlive-science)
#
# Run from the repository root, or from anywhere — the script resolves paths
# relative to its own location.

set -euo pipefail

# ── paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCS_DIR="$REPO_ROOT/docs"
OUT_DIR="$DOCS_DIR/pdf"

# ── argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) OUT_DIR="$2"; shift 2 ;;
        --out=*) OUT_DIR="${1#--out=}"; shift ;;
        -h|--help)
            echo "Usage: $0 [--out DIR]"
            echo "  --out DIR   Output directory for PDFs (default: docs/pdf/)"
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── dependency check ──────────────────────────────────────────────────────────
if ! command -v pandoc &>/dev/null; then
    echo "Error: pandoc not found. Install it with: sudo apt install pandoc" >&2
    exit 1
fi
if ! command -v lualatex &>/dev/null; then
    echo "Error: lualatex not found. Install it with:" >&2
    echo "  sudo apt install texlive-luatex texlive-latex-extra texlive-fonts-recommended texlive-science" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# ── manual definitions: "source|toc-title|output-stem" ───────────────────────
declare -a MANUALS=(
    "EMULATOR_GUIDE_FR.md|Table des matières|EMULATOR_GUIDE_FR"
    "EMULATOR_GUIDE_EN.md|Table of Contents|EMULATOR_GUIDE_EN"
    "SMAKY6_USER_GUIDE_FR.md|Table des matières|SMAKY6_USER_GUIDE_FR"
    "SMAKY6_USER_GUIDE_EN.md|Table of Contents|SMAKY6_USER_GUIDE_EN"
    "SMAKY6_FUNNY_GUIDE_FR.md|Table des matières|SMAKY6_FUNNY_GUIDE_FR"
    "SMAKY6_FUNNY_GUIDE_EN.md|Table of Contents|SMAKY6_FUNNY_GUIDE_EN"
    "SMAKY6_KIDS_FR.md|Table des matières|SMAKY6_KIDS_FR"
    "SMAKY6_KIDS_EN.md|Table of Contents|SMAKY6_KIDS_EN"
)

# ── build ─────────────────────────────────────────────────────────────────────
PASS=0
FAIL=0

for entry in "${MANUALS[@]}"; do
    IFS='|' read -r src_file toc_title out_stem <<< "$entry"
    src="$DOCS_DIR/$src_file"
    out="$OUT_DIR/$out_stem.pdf"

    if [[ ! -f "$src" ]]; then
        echo "  SKIP  $src_file (file not found)"
        continue
    fi

    printf "  %-40s → %s ... " "$src_file" "$out_stem.pdf"

    if pandoc "$src" \
        --pdf-engine=lualatex \
        -V "mainfont=Liberation Serif" \
        -V "sansfont=Liberation Sans" \
        -V "monofont=DejaVu Sans Mono" \
        -V fontsize=11pt \
        -V geometry:margin=2.5cm \
        -V colorlinks=true \
        -V linkcolor=NavyBlue \
        -V urlcolor=NavyBlue \
        --toc --toc-depth=2 \
        -V "toc-title=$toc_title" \
        -o "$out" 2>/tmp/generate_pdfs_err.log; then
        echo "OK"
        (( PASS++ )) || true
    else
        echo "FAILED"
        cat /tmp/generate_pdfs_err.log >&2
        (( FAIL++ )) || true
    fi
done

# ── summary ───────────────────────────────────────────────────────────────────
echo ""
echo "Done: $PASS succeeded, $FAIL failed."
echo "PDFs written to: $OUT_DIR"
[[ $FAIL -eq 0 ]]
