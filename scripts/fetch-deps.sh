#!/usr/bin/env bash
# Fetches the pinned third-party sources into third_party/<name>.
#
# Each repository is cloned shallow (depth 1) and blobless, then checked out
# sparsely so that only the directories sillok builds are downloaded. The
# checked-out commit must equal the pinned SHA, which is what makes the
# download verifiable: git checks every object against its hash.
#
# Usage: scripts/fetch-deps.sh [dir]   (default: <repo>/third_party)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-$root/third_party}"

# name | url | tag | commit | sparse directories (cone mode; root files are always included)
deps=(
  "whisper.cpp|https://github.com/ggml-org/whisper.cpp.git|v1.9.5|d1be6fde11ac6e0407606b4e42fe72d34add8037|cmake include src ggml/cmake ggml/include ggml/src/ggml-cpu ggml/src/ggml-vulkan ggml/src/ggml-metal ggml/src/ggml-blas"
  "NeMo-Speech.cpp|https://github.com/NVIDIA/NeMo-Speech.cpp.git|v0.2.0|6a3ca369370782acee0dd155e34394ba60b920c4|include src/asr/diar src/asr/encoder src/asr/features src/common src/runtime/ggml"
  "json|https://github.com/nlohmann/json.git|v3.12.0|55f93686c01528224f448c19128836e7df245f72|cmake include LICENSES"
  "googletest|https://github.com/google/googletest.git|v1.18.0|063de7e9578f82b369302001269680b4b1553359|googletest"
)

mkdir -p "$dest"
total=0
for entry in "${deps[@]}"; do
  IFS='|' read -r name url tag commit paths <<<"$entry"
  dir="$dest/$name"
  if [[ -d "$dir/.git" && "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" == "$commit" ]]; then
    # shellcheck disable=SC2086
    git -C "$dir" sparse-checkout set --cone $paths
  else
    rm -rf "$dir"
    git init --quiet "$dir"
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch --quiet --depth 1 --filter=blob:none origin "$commit"
    # shellcheck disable=SC2086
    git -C "$dir" sparse-checkout set --cone $paths
    git -C "$dir" checkout --quiet --detach FETCH_HEAD
  fi
  actual="$(git -C "$dir" rev-parse HEAD)"
  if [[ "$actual" != "$commit" ]]; then
    echo "error: $name is at $actual, expected $commit" >&2
    exit 1
  fi
  bytes="$(du -sb "$dir/.git" | cut -f1)"
  total=$((total + bytes))
  printf '%-18s %-8s %s  downloaded %6.1f MB\n' "$name" "$tag" "${commit:0:12}" "$(echo "$bytes / 1000000" | bc -l)"
done
printf 'total downloaded %.1f MB\n' "$(echo "$total / 1000000" | bc -l)"
