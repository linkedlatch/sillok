#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE_VERSION=3.31.6
CMAKE_SHA256=330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d
CMAKE_DIR="$ROOT/.tools/cmake-$CMAKE_VERSION-macos-universal"

if command -v cmake >/dev/null; then
    command -v cmake
    exit 0
fi

if [ ! -x "$CMAKE_DIR/CMake.app/Contents/bin/cmake" ]; then
    mkdir -p "$ROOT/.tools"
    tarball="$ROOT/.tools/cmake.tar.gz"
    curl -fL -o "$tarball" "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-macos-universal.tar.gz"
    echo "$CMAKE_SHA256  $tarball" | shasum -a 256 -c -
    tar -xzf "$tarball" -C "$ROOT/.tools"
    rm "$tarball"
fi
echo "$CMAKE_DIR/CMake.app/Contents/bin/cmake"
