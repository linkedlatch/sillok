"""Builds a lecture-like evaluation recording with known text.

Usage: python3 make_lecture_set.py LECTURE.wav LECTURE.json AUDIO_DIR
LECTURE.wav is a real lecture (16 kHz mono PCM16, e.g. ffmpeg -i lecture.m4a
-ac 1 -ar 16000 lecture.wav) and LECTURE.json sillok's transcript of it, which
tells where its pauses are. FLEURS Korean sentences (AUDIO_DIR/sets/
fleurs_ko.*, from make_sets.py) are spliced into the lecture's pauses, each
in the lecture's own room noise, at a set level above that noise and with a
synthetic room echo, near or far; stretches of noise alone are spliced in to
count text made up in long pauses. Writes AUDIO_DIR/sets/lecture.wav and
lecture.json. Needs numpy, unlike the other scripts; deterministic.
"""
import json
import os
import sys
import wave

import numpy as np

RATE = 16000
LEVELS_DB = [3, 6, 10, 20]            # sentence above the room noise
ROOMS = {                             # name: (RT60 in s, direct-to-reverberant in dB)
    "near": (0.5, 5.0),               # someone close to the recorder
    "far": (1.0, -5.0),               # a question from the back of the room
}
PER_CONDITION = 6
NOISE_ONLY = 8
NOISE_ONLY_SECONDS = 8.0
PAD_SECONDS = 1.0                     # noise before and after each sentence
SPACING_SECONDS = 30.0                # lecture between splices, at least


def read_wav(path):
    with wave.open(path) as file:
        assert file.getnchannels() == 1 and file.getframerate() == RATE, path
        assert file.getsampwidth() == 2, path
        return np.frombuffer(file.readframes(file.getnframes()), dtype=np.int16) / 32768.0


def room_response(rt60, drr_db, rng):
    """Direct sound, then exponentially decaying noise from 2.5 ms on."""
    length = int(RATE * rt60 * 1.2)
    t = np.arange(length) / RATE
    tail = rng.standard_normal(length) * np.exp(-6.9 * t / rt60)
    tail[: int(0.0025 * RATE)] = 0.0
    tail *= 10 ** (-drr_db / 20) / np.sqrt(np.sum(tail ** 2))
    tail[0] = 1.0
    return tail


def convolve(x, h):
    n = 1 << (len(x) + len(h) - 2).bit_length()
    return np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(h, n), n)[: len(x) + len(h) - 1]


def active_rms(x):
    """RMS of the 20 ms frames within 30 dB of the loudest one."""
    frame = RATE // 50
    energy = (x[: len(x) // frame * frame].reshape(-1, frame) ** 2).mean(axis=1)
    return float(np.sqrt(energy[energy > energy.max() * 1e-3].mean()))


def room_noise(lecture, segments):
    """The lecture's steady noise: the 200 ms frames of the gaps between its
    sentences within 6 dB of the gaps' median level (gaps also hold word
    endings and other sounds)."""
    pieces = []
    for left, right in zip(segments, segments[1:]):
        begin, end = left["end_ms"] / 1000 + 0.3, right["start_ms"] / 1000 - 0.3
        if end - begin >= 0.7:
            pieces.append(lecture[int(begin * RATE): int(end * RATE)])
    gaps = np.concatenate(pieces)
    frame = RATE // 5
    frames = gaps[: len(gaps) // frame * frame].reshape(-1, frame)
    energy = (frames ** 2).mean(axis=1)
    return frames[energy < np.median(energy) * 4].reshape(-1)


def main(lecture_wav, lecture_json, audio_dir):
    rng = np.random.default_rng(7)
    sets_dir = os.path.join(audio_dir, "sets")
    lecture = read_wav(lecture_wav)
    with open(lecture_json, encoding="utf-8") as file:
        segments = json.load(file)["segments"]
    noise_bed = room_noise(lecture, segments)
    noise_rms = float(np.sqrt(np.mean(noise_bed ** 2)))
    noise_at = 0

    def noise(seconds):
        nonlocal noise_at
        n = int(seconds * RATE)
        out = noise_bed[(noise_at + np.arange(n)) % len(noise_bed)]
        noise_at += n
        return out

    fleurs = read_wav(os.path.join(sets_dir, "fleurs_ko.wav"))
    with open(os.path.join(sets_dir, "fleurs_ko.json"), encoding="utf-8") as file:
        utterances = json.load(file)["utterances"]
    order = rng.permutation(len(utterances))
    conditions = [(level, room) for level in LEVELS_DB for room in ROOMS]
    splices = [{"kind": "speech", "level_db": conditions[k % len(conditions)][0],
                "room": conditions[k % len(conditions)][1], "utterance": int(order[k])}
               for k in range(PER_CONDITION * len(conditions))]
    splices += [{"kind": "noise"}] * NOISE_ONLY
    splices = [splices[i] for i in rng.permutation(len(splices))]

    # Midpoints of lecture pauses of a second or more, spaced out.
    points = []
    for left, right in zip(segments, segments[1:]):
        if right["start_ms"] - left["end_ms"] >= 1000:
            middle = (left["end_ms"] + right["start_ms"]) / 2000
            if not points or middle - points[-1] >= SPACING_SECONDS:
                points.append(middle)
    if len(points) < len(splices):
        sys.exit(f"the lecture has {len(points)} places for {len(splices)} splices")

    out, reference = [], []
    position, cursor = 0.0, 0
    for point, splice in zip(points, splices):
        cut = int(point * RATE)
        out.append(lecture[cursor:cut])
        position += (cut - cursor) / RATE
        cursor = cut
        if splice["kind"] == "noise":
            out.append(noise(NOISE_ONLY_SECONDS))
            reference.append({"kind": "noise", "start": round(position, 3),
                              "end": round(position + NOISE_ONLY_SECONDS, 3)})
            position += NOISE_ONLY_SECONDS
            continue
        u = utterances[splice["utterance"]]
        speech = fleurs[int((u["start"] - 0.2) * RATE): int((u["end"] + 0.2) * RATE)]
        speech = convolve(speech, room_response(*ROOMS[splice["room"]], rng))
        speech *= noise_rms * 10 ** (splice["level_db"] / 20) / active_rms(speech)
        body = noise(PAD_SECONDS * 2 + len(speech) / RATE)
        body[int(PAD_SECONDS * RATE): int(PAD_SECONDS * RATE) + len(speech)] += speech
        out.append(body)
        reference.append({"kind": "speech", "text": u["text"], "language": "ko",
                          "level_db": splice["level_db"], "room": splice["room"],
                          "start": round(position + PAD_SECONDS, 3),
                          "end": round(position + PAD_SECONDS + len(speech) / RATE, 3)})
        position += len(body) / RATE
    out.append(lecture[cursor: int((points[len(splices) - 1] + SPACING_SECONDS) * RATE)])
    audio = np.concatenate(out)
    audio *= min(1.0, 0.99 / np.max(np.abs(audio)))

    with wave.open(os.path.join(sets_dir, "lecture.wav"), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes((np.clip(audio, -1, 1) * 32767).astype(np.int16).tobytes())
    with open(os.path.join(sets_dir, "lecture.json"), "w", encoding="utf-8") as file:
        json.dump({"duration": len(audio) / RATE,
                   "noise_floor_dbfs": round(20 * np.log10(noise_rms), 1),
                   "splices": reference}, file, ensure_ascii=False, indent=1)
    print(f"lecture: {len(audio) / RATE / 60:.1f} min, {len(reference)} splices, "
          f"room noise {20 * np.log10(noise_rms):.1f} dBFS")


if __name__ == "__main__":
    main(*sys.argv[1:4])
