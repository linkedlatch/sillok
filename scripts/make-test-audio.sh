#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/fixtures"
mkdir -p "$OUT"
KOREAN="오늘 강의에서는 포맷 스트링 버그가 어떻게 발생하는지 살펴보고, 해커가 이 취약점을 이용해 메모리의 값을 덮어쓰는 과정을 차근차근 설명하겠습니다. 마지막으로 실제 사례를 통해 개발자가 주의해야 할 점을 정리하겠습니다."
ENGLISH="In today's lecture we will look at how format string bugs happen, and explain step by step how an attacker can use this vulnerability to overwrite values in memory. Finally, we will summarize what developers should watch out for, using a real world example."

speak() {
    local voice=$1 name=$2 text=$3
    say -v "$voice" -o "${OUT:?}/${name:?}.aiff" "$text"
    afconvert -f WAVE -d LEI16@16000 -c 1 "${OUT:?}/${name:?}.aiff" "${OUT:?}/${name:?}.wav"
    rm -f "${OUT:?}/${name:?}.aiff"
}

speak Samantha en "The quick brown fox jumps over the lazy dog."
speak Yuna ko "$KOREAN"
speak Yuna ko-long "$KOREAN"
speak Samantha en-long "$ENGLISH"
ls "$OUT"
