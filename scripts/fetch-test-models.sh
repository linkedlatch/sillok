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

fetch ggml-silero-v6.2.0.bin 2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987 "$VAD_BASE"
fetch ggml-large-v3-turbo-q5_0.bin 394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2
fetch ggml-large-v3-turbo-encoder.mlmodelc.zip 84bedfe895bd7b5de6e8e89a0803dfc5addf8c0c5bc4c937451716bf7cf7988a
fetch Nemotron-3-Diarization.q8_0.gguf 08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1 "https://huggingface.co/nvidia/Nemotron-3-Diarization/resolve/f667ed73aee57d40cc39428eb768b4fd87a0a29e"

for zip in "$DEST"/*-encoder.mlmodelc.zip; do
    [ -d "${zip%.zip}" ] || ditto -x -k "$zip" "$DEST"
done
