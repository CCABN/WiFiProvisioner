#!/usr/bin/env python3
"""
PlatformIO SCons build script to embed HTML files into C++ header.
This script runs before compilation to generate src/html_template.h
"""

Import("env")
import subprocess
import os
from pathlib import Path

def embed_html_callback(*args, **kwargs):
    """Callback to run the HTML embedding script"""
    print("Running HTML embedding script...")

    # Get the library directory (where this script is located)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    embed_script = script_dir / "embed_html.py"

    try:
        # Run the embedding script
        result = subprocess.run([
            "python3", str(embed_script)
        ], capture_output=True, text=True, cwd=str(project_root))

        if result.returncode == 0:
            print(result.stdout)
        else:
            print(f"Error running embed script: {result.stderr}")

    except Exception as e:
        print(f"Failed to run HTML embedding script: {e}")

# Add the callback to run before compilation
env.AddPreAction("buildprog", embed_html_callback)