"""Builds the evaluation recordings from the fetched test audio.

Usage: python3 -I make_sets.py AUDIO_DIR
Reads AUDIO_DIR/fleurs and AUDIO_DIR/download (scripts/fetch-test-audio.sh)
and writes AUDIO_DIR/sets/<name>.wav (16 kHz mono PCM16) with
<name>.json references. Deterministic: the same inputs give the same sets.
"""
import array
import json
import os
import random
import struct
import sys
import wave

RATE = 16000


def read_wav(path):
    """Samples of a 16 kHz mono WAV (PCM16 or float32) as floats."""
    data = open(path, "rb").read()
    position, fmt = 12, None
    while position + 8 <= len(data):
        chunk, size = data[position:position + 4], struct.unpack("<I", data[position + 4:position + 8])[0]
        body = data[position + 8:position + 8 + size]
        if chunk == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif chunk == b"data":
            encoding, channels, rate, _, _, bits = fmt
            assert channels == 1 and rate == RATE, path
            if encoding == 3 and bits == 32:
                return array.array("f", body).tolist()
            samples = array.array("h", body)
            return [s / 32768.0 for s in samples]
        position += 8 + size + (size & 1)
    raise ValueError(f"no audio in {path}")


def write_wav(path, samples):
    pcm = array.array("h", (max(-32768, min(32767, int(round(s * 32767)))) for s in samples))
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


def fleurs(audio_dir, lang):
    """Utterances of one FLEURS language that were extracted, in file order."""
    meta = {}
    with open(os.path.join(audio_dir, "download", f"{lang}-test.tsv"), encoding="utf-8") as tsv:
        for line in tsv:
            fields = line.rstrip("\n").split("\t")
            meta[fields[1]] = {"text": fields[2], "gender": fields[6]}
    folder = os.path.join(audio_dir, "fleurs", lang)
    return [dict(meta[name], path=os.path.join(folder, name), language=lang[:2])
            for name in sorted(os.listdir(folder)) if name in meta]


def concatenate(utterances, gaps):
    """One recording of `utterances` separated by `gaps` (seconds), with the
    reference span of each utterance."""
    samples, reference = [0.0] * int(0.5 * RATE), []
    for utterance, gap in zip(utterances, gaps):
        audio = read_wav(utterance["path"])
        start = len(samples) / RATE
        samples.extend(audio)
        entry = {k: v for k, v in utterance.items() if k != "path"}
        entry.update(start=round(start, 3), end=round(len(samples) / RATE, 3))
        reference.append(entry)
        samples.extend([0.0] * int(gap * RATE))
    return samples, reference


def save(sets_dir, name, samples, reference):
    write_wav(os.path.join(sets_dir, name + ".wav"), samples)
    with open(os.path.join(sets_dir, name + ".json"), "w", encoding="utf-8") as out:
        json.dump({"duration": len(samples) / RATE, "utterances": reference}, out,
                  ensure_ascii=False, indent=1)
    print(f"{name}: {len(samples) / RATE / 60:.1f} min, {len(reference)} utterances")


def main(audio_dir):
    sets_dir = os.path.join(audio_dir, "sets")
    os.makedirs(sets_dir, exist_ok=True)
    rng = random.Random(20261028)
    korean, english = fleurs(audio_dir, "ko_kr"), fleurs(audio_dir, "en_us")

    for name, pool in (("fleurs_ko", korean), ("fleurs_en", english)):
        save(sets_dir, name, *concatenate(pool, [1.5] * len(pool)))

    # Language switches at sentence boundaries with conversational pauses.
    pattern = [("ko", 4), ("en", 3), ("ko", 2), ("en", 4), ("ko", 3), ("en", 2),
               ("ko", 4), ("en", 3), ("ko", 2)]
    pools = {"ko": list(korean), "en": list(english)}
    utterances = [pools[lang].pop(0) for lang, count in pattern for _ in range(count)]
    save(sets_dir, "switch", *concatenate(utterances, [rng.uniform(0.6, 1.2) for _ in utterances]))

    # Two Korean voices taking turns: FLEURS has no speaker ids, so gender
    # stands in for the speaker.
    male = [u for u in korean if u["gender"] == "MALE"]
    female = [u for u in korean if u["gender"] == "FEMALE"]
    turns = [u for pair in zip(male, female) for u in pair]
    save(sets_dir, "korean_two_speakers", *concatenate(turns, [rng.uniform(0.3, 0.8) for _ in turns]))

    # Long recordings for memory and speed: the meeting and both FLEURS sets,
    # repeated up to the length.
    pool = read_wav(os.path.join(audio_dir, "download", "ES2004a.Mix-Headset.wav"))
    for name in ("fleurs_ko", "fleurs_en"):
        pool += read_wav(os.path.join(sets_dir, name + ".wav"))
    for name, hours in (("long_1h", 1), ("long_3h", 3)):
        total = hours * 3600 * RATE
        samples = (pool * (total // len(pool) + 1))[:total]
        save(sets_dir, name, samples, [])


if __name__ == "__main__":
    main(sys.argv[1])
