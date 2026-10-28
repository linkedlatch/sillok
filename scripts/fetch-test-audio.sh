#!/usr/bin/env bash
# Fetches the public test recordings into tests/data/audio (not committed).
#
# - FLEURS (google/fleurs, CC BY 4.0): Korean and English test sets with
#   reference transcripts. Only the first 48 MB of each audio archive is
#   downloaded (of 214 MB and 290 MB); the complete utterances in it are used.
# - AMI meeting ES2004a (CC BY 4.0): headset mix with the speaker reference
#   of pyannote/AMI-diarization-setup.
# Every download is pinned to a revision and checked against its SHA-256.
#
# Usage: scripts/fetch-test-audio.sh [dir]   (default: <repo>/tests/data/audio)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-$root/tests/data/audio}"
fleurs="https://huggingface.co/datasets/google/fleurs/resolve/70bb2e84b976b7e960aa89f1c648e09c59f894dd/data"
ami_setup="https://raw.githubusercontent.com/pyannote/AMI-diarization-setup/67c2d539286e89f68952d5dcf83912bd9f01dfae"
ami_audio="https://groups.inf.ed.ac.uk/ami/AMICorpusMirror/amicorpus/ES2004a/audio"

# file | url | sha256 | byte range (empty for the whole file)
files=(
  "ko_kr-test.tsv|$fleurs/ko_kr/test.tsv|cf2f7c8765f6203e3c46ef620d5e936d3f331b6e8865455557777dd1347517f5|"
  "ko_kr-test-head.tar.gz|$fleurs/ko_kr/audio/test.tar.gz|9070bdc110da8ddc03d37e40371209fe117ddc7ea2b10d8b70a5aebcbf2b0a7a|0-47999999"
  "en_us-test.tsv|$fleurs/en_us/test.tsv|74c046239374deeb60fa63f258f907388093a32bcaa3140965f70ef05c79f7ca|"
  "en_us-test-head.tar.gz|$fleurs/en_us/audio/test.tar.gz|b5d0419e4d7d74cad39f71ae8d8076b87711ea66e098c4fd5878e7bf1b9b6b88|0-47999999"
  "ES2004a.rttm|$ami_setup/only_words/rttms/test/ES2004a.rttm|9869c6146c2fd9595403edb36c2caeda65c12ffa2c0af4ce48d6814b673fd5a9|"
  "ES2004a.uem|$ami_setup/uems/test/ES2004a.uem|514373eae2841ddf15fb07938a7d74bbecc2acad8afc2a28a93e0f7b24bfb446|"
  "ES2004a.Mix-Headset.wav|$ami_audio/ES2004a.Mix-Headset.wav|3e2560b19bee6952c7c7ce041b0f1ea8a7ea9468044c4eea79d2a2c67e24ab0f|"
)

mkdir -p "$dest/download"
total=0
for entry in "${files[@]}"; do
  IFS='|' read -r name url sha range <<<"$entry"
  path="$dest/download/$name"
  if [[ ! -f "$path" ]] || [[ "$(sha256sum "$path" | cut -d' ' -f1)" != "$sha" ]]; then
    curl -sSL --fail --retry 3 ${range:+-r "$range"} -o "$path.part" "$url"
    mv "$path.part" "$path"
  fi
  if [[ "$(sha256sum "$path" | cut -d' ' -f1)" != "$sha" ]]; then
    echo "error: $name does not match its pinned SHA-256" >&2
    exit 1
  fi
  total=$((total + $(stat -c %s "$path")))
done
awk -v b="$total" 'BEGIN { printf "downloaded %.1f MB\n", b / 1e6 }'

for lang in ko_kr en_us; do
  printf '%s: ' "$lang"
  python3 -I "$root/scripts/eval/extract_head.py" \
    "$dest/download/$lang-test-head.tar.gz" "$dest/fleurs/$lang"
done
