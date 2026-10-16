#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/models"
COMMIT=5359861c739e955e79d9a303bcbc70fb988958b1
BASE="https://huggingface.co/ggerganov/whisper.cpp/resolve/$COMMIT"
VAD_BASE="https://huggingface.co/ggml-org/whisper-vad/resolve/9ffd54a1e1ee413ddf265af9913beaf518d1639b"

fetch() {
    local name=$1 sha=$2 base=${3:-$BASE}
    if [ -f "$DEST/$name" ] && echo "$sha  $DEST/$name" | shasum -a 256 -c - >/dev/null 2>&1; then
        echo "ok $name"
        return
    fi
    mkdir -p "$DEST"
    curl -fL -C - -o "$DEST/$name.part" "$base/$name"
    echo "$sha  $DEST/$name.part" | shasum -a 256 -c - >/dev/null
    mv "$DEST/$name.part" "$DEST/$name"
    echo "ok $name"
}

fetch ggml-tiny.bin be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21
fetch ggml-tiny-encoder.mlmodelc.zip c88cbd2648e1f5415092bcf5256add463a0f19943e6938f46e8d4ffdebd47739
fetch ggml-silero-v6.2.0.bin 2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987 "$VAD_BASE"

if [ ! -d "$DEST/ggml-tiny-encoder.mlmodelc" ]; then
    ditto -x -k "$DEST/ggml-tiny-encoder.mlmodelc.zip" "$DEST"
fi
