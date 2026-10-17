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
speak Yuna dialog-1 "안녕하세요, 오늘 회의에서는 다음 분기 출시 일정에 대해 이야기해 보겠습니다. 먼저 개발 진행 상황부터 공유해 주시겠어요?"
speak "Grandpa (한국어(한국))" dialog-2 "네, 현재 핵심 기능은 대부분 구현이 끝났고 테스트를 진행하고 있습니다. 다만 성능 문제가 조금 남아 있어서 이번 주 안에 해결할 예정입니다."
speak Yuna dialog-3 "좋습니다. 그러면 디자인 쪽은 어떻게 되고 있나요?"
speak "Rocko (한국어(한국))" dialog-4 "디자인은 최종 시안이 거의 확정되었습니다. 사용자 테스트 결과를 반영해서 버튼 배치와 색상을 조금 수정했습니다."
speak "Grandpa (한국어(한국))" dialog-5 "그 수정 사항이 개발 일정에 영향을 주지는 않을까요? 화면 구조가 바뀌면 작업이 늘어날 수 있어서요."
speak "Rocko (한국어(한국))" dialog-6 "구조는 그대로이고 스타일만 바뀌어서 큰 영향은 없을 겁니다."
speak Yuna dialog-7 "알겠습니다. 그럼 출시일은 예정대로 다음 달 말로 유지하겠습니다. 모두 수고하셨습니다."
ls "$OUT"
