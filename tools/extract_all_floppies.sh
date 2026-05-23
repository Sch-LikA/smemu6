#!/bin/bash
# Extract all floppy disk images from private/floppies/decoded into private/floppies/extracted/
# Each image is extracted to its own directory named after the source file

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

DECODED_DIR="$REPO_ROOT/private/floppies/decoded"
EXTRACTED_DIR="$REPO_ROOT/private/floppies/extracted"
EXTRACT_SCRIPT="$SCRIPT_DIR/extract_samos_image.py"

# Check if decoded directory exists
if [[ ! -d "$DECODED_DIR" ]]; then
    echo "Error: $DECODED_DIR does not exist"
    exit 1
fi

# Check if extract script exists
if [[ ! -f "$EXTRACT_SCRIPT" ]]; then
    echo "Error: $EXTRACT_SCRIPT not found"
    exit 1
fi

# Create extracted directory if it doesn't exist
mkdir -p "$EXTRACTED_DIR"

# Counter for processed images
count=0

# Iterate through all files in the decoded directory
for image_file in "$DECODED_DIR"/*.img; do
    # Skip if not a regular file
    if [[ ! -f "$image_file" ]]; then
        continue
    fi

    # Get the base filename without extension
    filename=$(basename "$image_file")
    basename_no_ext="${filename%.*}"

    # Define output directory
    output_dir="$EXTRACTED_DIR/$basename_no_ext"

    # Skip if already extracted
    if [[ -d "$output_dir" ]]; then
        echo "Skipping $filename (already extracted to $output_dir)"
        continue
    fi

    echo "Extracting $filename to $output_dir..."
    python3 "$EXTRACT_SCRIPT" "$image_file" "$output_dir" || {
        echo "Error extracting $filename"
        exit 1
    }

    count=$((count + 1))
done

if [[ $count -eq 0 ]]; then
    echo "No new images to extract (all already present or no images found)"
    exit 0
fi

echo "Successfully extracted $count floppy image(s)"
