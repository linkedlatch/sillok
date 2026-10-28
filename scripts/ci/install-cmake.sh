#!/usr/bin/env bash
# Installs the pinned CMake and Ninja from PyPI into a virtual environment and
# puts it on the PATH of the following CI steps (system Pythons may refuse a
# global pip install).
set -euo pipefail
python=$(command -v python3 || command -v python)
temp="$RUNNER_TEMP"
command -v cygpath > /dev/null && temp=$(cygpath -u "$temp")  # Git Bash on Windows
venv="$temp/build-tools"
"$python" -m venv "$venv"
bin="$venv/bin"
[[ -d "$bin" ]] || bin="$venv/Scripts"  # Windows
"$bin/python" -m pip install --quiet "cmake==$CMAKE_VERSION" "ninja==$NINJA_VERSION"
command -v cygpath > /dev/null && bin=$(cygpath -w "$bin")
echo "$bin" >> "$GITHUB_PATH"
