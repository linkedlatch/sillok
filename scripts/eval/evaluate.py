"""Scores a sillok transcript (JSON from `sillok transcribe --format json`).

Usage:
  python3 -I evaluate.py accuracy SET.json TRANSCRIPT.json
  python3 -I evaluate.py switch SET.json TRANSCRIPT.json
  python3 -I evaluate.py speakers SET.json TRANSCRIPT.json
  python3 -I evaluate.py ami TRANSCRIPT.json DOWNLOAD_DIR [OFFSET_SECONDS]
Prints one JSON object with the scores.
"""
import json
import os
import sys
import xml.etree.ElementTree as ElementTree

import metrics


def load(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def hypothesis_words(transcript):
    """Words with times in seconds, their segment's language and speaker."""
    words = []
    for segment in transcript["segments"]:
        for word in segment["words"]:
            words.append({"text": word["text"], "start": word["start_ms"] / 1000,
                          "end": word["end_ms"] / 1000,
                          "language": segment["language"],
                          "speaker": word.get("speaker", segment.get("speaker"))})
    return words


def by_utterance(reference, words):
    """Hypothesis words grouped by the utterance whose span (extended to the
    middle of the surrounding pauses) contains their midpoint."""
    utterances = reference["utterances"]
    bounds = [0.0]
    for left, right in zip(utterances, utterances[1:]):
        bounds.append((left["end"] + right["start"]) / 2)
    bounds.append(float("inf"))
    groups = [[] for _ in utterances]
    for word in words:
        middle = (word["start"] + word["end"]) / 2
        for i in range(len(utterances)):
            if bounds[i] <= middle < bounds[i + 1]:
                groups[i].append(word)
                break
    return groups


def tokens(text, language):
    return metrics.korean_characters(text) if language == "ko" else metrics.english_words(text)


def accuracy(reference, transcript):
    groups = by_utterance(reference, hypothesis_words(transcript))
    pairs = []
    for utterance, words in zip(reference["utterances"], groups):
        language = utterance["language"]
        hypothesis = " ".join(w["text"] for w in words)
        pairs.append((tokens(utterance["text"], language), tokens(hypothesis, language)))
    rate, errors, total = metrics.error_rate(pairs)
    unit = "cer" if reference["utterances"][0]["language"] == "ko" else "wer"
    return {unit: round(rate, 4), "errors": errors, "reference_tokens": total}


def switch(reference, transcript):
    utterances = reference["utterances"]
    groups = by_utterance(reference, hypothesis_words(transcript))
    decided = []
    for words in groups:
        weight = {}
        for word in words:
            weight[word["language"]] = weight.get(word["language"], 0) + word["end"] - word["start"]
        decided.append(max(weight, key=weight.get) if weight else None)
    correct_time = sum(u["end"] - u["start"] for u, d in zip(utterances, decided) if d == u["language"])
    total_time = sum(u["end"] - u["start"] for u in utterances)
    # Blocks of consecutive utterances in one language.
    blocks, current = [], []
    for i, utterance in enumerate(utterances):
        if current and utterances[current[-1]]["language"] != utterance["language"]:
            blocks.append(current)
            current = []
        current.append(i)
    blocks.append(current)
    switches = [b for b in blocks[1:]]
    detected = sum(1 for b in switches if decided[b[0]] == utterances[b[0]]["language"])
    detected_in_block = sum(
        1 for b in switches
        if any(decided[i] == utterances[i]["language"] for i in b))
    false_switches = 0
    for block in blocks:
        languages = [decided[i] for i in block if decided[i] is not None]
        false_switches += sum(1 for a, b in zip(languages, languages[1:]) if a != b)
    pairs = [(tokens(u["text"], u["language"]),
              tokens(" ".join(w["text"] for w in words), u["language"]))
             for u, words in zip(utterances, groups)]
    return {"language_accuracy_by_time": round(correct_time / total_time, 4),
            "switches": len(switches),
            "switches_detected_at_first_utterance": detected,
            "switches_detected_within_block": detected_in_block,
            "false_switches_within_blocks": false_switches,
            "token_error_rate": round(metrics.error_rate(pairs)[0], 4),
            "utterance_languages": "".join((d or "-")[0] for d in decided)}


def turns_of(transcript):
    return [(s["start_ms"] / 1000, s["end_ms"] / 1000, s["speaker"])
            for s in transcript["segments"] if s.get("speaker") is not None]


def speakers(reference, transcript):
    utterances = reference["utterances"]
    ref_turns = [(u["start"], u["end"], u["gender"]) for u in utterances]
    hyp_turns = turns_of(transcript)
    der = metrics.der(ref_turns, hyp_turns, 0.0, reference["duration"])
    # Many-to-one: each hypothesis speaker counts as the gender it overlaps
    # most, so several voices per gender are not penalized.
    overlap = {}
    for h0, h1, speaker in hyp_turns:
        for r0, r1, gender in ref_turns:
            shared = min(h1, r1) - max(h0, r0)
            if shared > 0:
                overlap.setdefault(speaker, {}).setdefault(gender, 0.0)
                overlap[speaker][gender] += shared
    label = {s: max(g, key=g.get) for s, g in overlap.items()}
    groups = by_utterance(reference, hypothesis_words(transcript))
    attributed = 0
    for utterance, words in zip(utterances, groups):
        weight = {}
        for word in words:
            gender = label.get(word["speaker"])
            weight[gender] = weight.get(gender, 0) + word["end"] - word["start"]
        if weight and max(weight, key=weight.get) == utterance["gender"]:
            attributed += 1
    return {"der": round(der[0], 4), "miss": round(der[1], 4), "false_alarm": round(der[2], 4),
            "confusion": round(der[3], 4),
            "hypothesis_speakers": len({s for _, _, s in hyp_turns}),
            "utterances_attributed_to_right_voice": round(attributed / len(utterances), 4)}


def ami_words(download_dir, offset):
    words = []
    for participant in "ABCD":
        path = os.path.join(download_dir, f"ES2004a.{participant}.words.xml")
        for element in ElementTree.parse(path).getroot():
            if element.tag != "w" or element.get("punc") == "true" or not element.text:
                continue
            words.append({"text": element.text, "start": float(element.get("starttime")) - offset,
                          "end": float(element.get("endtime")) - offset, "speaker": participant})
    return sorted(words, key=lambda w: w["start"])


def ami(transcript, download_dir, offset):
    uem = open(os.path.join(download_dir, "ES2004a.uem")).read().split()
    start, end = float(uem[2]) - offset, float(uem[3]) - offset
    duration = transcript["duration_ms"] / 1000
    start, end = max(0.0, start), min(end, duration)
    ref_turns = []
    for line in open(os.path.join(download_dir, "ES2004a.rttm")):
        fields = line.split()
        t0 = float(fields[3]) - offset
        ref_turns.append((t0, t0 + float(fields[4]), fields[7]))
    ref_turns = [(max(t0, start), min(t1, end), s) for t0, t1, s in ref_turns if t1 > start and t0 < end]
    hyp_turns = turns_of(transcript)
    result = {}
    if hyp_turns:
        der = metrics.der(ref_turns, hyp_turns, start, end)
        result.update(der=round(der[0], 4), miss=round(der[1], 4),
                      false_alarm=round(der[2], 4), confusion=round(der[3], 4),
                      hypothesis_speakers=len({s for _, _, s in hyp_turns}))

    reference = [w for w in ami_words(download_dir, offset) if start <= w["start"] < end]
    hypothesis = hypothesis_words(transcript)
    normalize = lambda w: " ".join(metrics.english_words(w["text"]))
    pairs = [(r, h) for r, h in metrics.align_words(reference, hypothesis, normalize) if normalize(r)]
    errors = [abs(h["start"] - r["start"]) for r, h in pairs]
    result.update(words_matched=len(pairs), reference_words=len(reference),
                  start_error_median=round(metrics.percentile(errors, 0.5), 3),
                  start_error_p90=round(metrics.percentile(errors, 0.9), 3),
                  start_within_0_5s=round(sum(e <= 0.5 for e in errors) / max(len(errors), 1), 4))
    if hyp_turns:
        # Speaker of each matched word, under the mapping that DER uses.
        participant = {}
        for line in open(os.path.join(download_dir, "ES2004a.rttm")):
            pass  # RTTM speaker names are meeting ids; map via shared time below
        mapping = metrics.speaker_mapping(
            [(w["start"], w["end"], w["speaker"]) for w in reference], hyp_turns, start, end)
        right = sum(1 for r, h in pairs if mapping.get(h["speaker"]) == r["speaker"])
        result["word_speaker_accuracy"] = round(right / max(len(pairs), 1), 4)
    return result


def main(argv):
    command = argv[1]
    if command == "ami":
        offset = float(argv[4]) if len(argv) > 4 else 0.0
        result = ami(load(argv[2]), argv[3], offset)
    else:
        reference, transcript = load(argv[2]), load(argv[3])
        result = {"accuracy": accuracy, "switch": switch, "speakers": speakers}[command](reference, transcript)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main(sys.argv)
