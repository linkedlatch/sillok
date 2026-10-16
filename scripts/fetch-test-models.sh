#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/models"
COMMIT=5359861c739e955e79d9a303bcbc70fb988958b1
BASE="https://huggingface.co/ggerganov/whisper.cpp/resolve/$COMMIT"

fetch() {
    local name=$1 sha=$2
    if [ -f "$DEST/$name" ] && echo "$sha  $DEST/$name" | shasum -a 256 -c - >/dev/null 2>&1; then
        echo "ok $name"
        return
    fi
    mkdir -p "$DEST"
    curl -fL -C - -o "$DEST/$name.part" "$BASE/$name"
    echo "$sha  $DEST/$name.part" | shasum -a 256 -c - >/dev/null
    mv "$DEST/$name.part" "$DEST/$name"
    echo "ok $name"
}

fetch ggml-tiny.bin be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21
fetch ggml-tiny-encoder.mlmodelc.zip c88cbd2648e1f5415092bcf5256add463a0f19943e6938f46e8d4ffdebd47739

if [ ! -d "$DEST/ggml-tiny-encoder.mlmodelc" ]; then
    ditto -x -k "$DEST/ggml-tiny-encoder.mlmodelc.zip" "$DEST"
fi
