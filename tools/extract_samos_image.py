#!/usr/bin/env python3
"""
Wrapper for smaky6_samos.py extract-all command.

This script maintains backward compatibility with the old extract_samos_image.py interface
while delegating to the unified smaky6_samos.py tool in the smaky6-tools repository.

Original functionality:
  extract_samos_image.py <image.dsk> <outdir>

Now delegates to:
  smaky6_samos.py extract-all <image.dsk> <outdir> --metadata --clear

The --metadata flag writes JSON sidecars, and --clear removes the output directory first,
matching the original behavior.
"""

import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: extract_samos_image.py <image.dsk> <outdir>", file=sys.stderr)
        return 2

    image_path = sys.argv[1]
    outdir = sys.argv[2]

    # Try to find smaky6_samos.py in sibling directory
    # Assume: smemu6/tools/extract_samos_image.py calls smaky6-tools/smaky6_samos.py
    import os
    import pathlib

    # Get script directory
    script_dir = pathlib.Path(__file__).parent.parent.parent  # ../.. from tools/
    
    # Try possible locations
    possible_paths = [
        script_dir / "smaky6-tools" / "smaky6_samos.py",
        pathlib.Path.home() / "dev" / "smaky6-tools" / "smaky6_samos.py",
        pathlib.Path("/opt/smaky6-tools/smaky6_samos.py"),
    ]
    
    samos_tool = None
    for path in possible_paths:
        if path.exists():
            samos_tool = str(path)
            break
    
    if samos_tool is None:
        # Fallback: try to import from PATH
        import shutil
        samos_tool = shutil.which("smaky6_samos.py")
        if samos_tool is None:
            print(
                "Error: Could not find smaky6_samos.py\n"
                "Install smaky6-tools or set SMAKY6_SAMOS_PATH environment variable",
                file=sys.stderr
            )
            return 1
    
    # Call smaky6_samos.py with extract-all, --metadata, and --clear flags
    cmd = [sys.executable, samos_tool, "extract-all", image_path, outdir, "--metadata", "--clear"]
    result = subprocess.run(cmd)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())