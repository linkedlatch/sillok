"""Scoring for the sillok evaluation: error rates, DER, word alignment.

Standard library only. Normalization is deliberately simple (no number or
spelling normalization), so format differences such as "15m" vs "15 meters"
count as errors.
"""
import difflib
import itertools
import re
import unicodedata


def _strip_punctuation(text):
    return "".join(c for c in text if not unicodedata.category(c)[0] in "PS")


def english_words(text):
    """Lower-cased words without punctuation; apostrophes inside words stay."""
    text = unicodedata.normalize("NFC", text).lower().replace("-", " ")
    words = []
    for word in text.split():
        word = word.replace("'", "\0")
        word = _strip_punctuation(word).replace("\0", "'").strip("'")
        if word:
            words.append(word)
    return words


def korean_characters(text):
    """Characters without spaces and punctuation. Parenthesized glosses such as
    "비슈케크(Bishkek)" are dropped: they are written, not read."""
    text = unicodedata.normalize("NFC", text)
    text = re.sub(r"\([^)]*\)", "", text).lower()
    return [c for c in _strip_punctuation(text) if not c.isspace()]


def edit_distance(reference, hypothesis):
    """Levenshtein distance between two sequences."""
    previous = list(range(len(hypothesis) + 1))
    for i, r in enumerate(reference, 1):
        current = [i] + [0] * len(hypothesis)
        for j, h in enumerate(hypothesis, 1):
            current[j] = min(previous[j] + 1, current[j - 1] + 1,
                             previous[j - 1] + (r != h))
        previous = current
    return previous[-1]


def error_rate(pairs):
    """Corpus error rate over (reference tokens, hypothesis tokens) pairs."""
    errors = sum(edit_distance(r, h) for r, h in pairs)
    total = sum(len(r) for r, _ in pairs)
    return errors / max(total, 1), errors, total


# --- diarization ---------------------------------------------------------

def _frames(turns, start, end, step):
    """For each frame of [start, end), the set of speakers active in it."""
    count = int(round((end - start) / step))
    frames = [set() for _ in range(count)]
    for t0, t1, speaker in turns:
        first = max(0, int((t0 - start) / step))
        last = min(count, int((t1 - start) / step))
        for i in range(first, last):
            frames[i].add(speaker)
    return frames


def speaker_mapping(reference, hypothesis, start, end, step=0.01):
    """Hypothesis -> reference speaker mapping with the most shared time."""
    ref_frames = _frames(reference, start, end, step)
    hyp_frames = _frames(hypothesis, start, end, step)
    ref_ids = sorted({s for _, _, s in reference})
    hyp_ids = sorted({s for _, _, s in hypothesis})
    overlap = {(h, r): 0 for h in hyp_ids for r in ref_ids}
    for rf, hf in zip(ref_frames, hyp_frames):
        for h in hf:
            for r in rf:
                overlap[(h, r)] += 1
    best, best_score = {}, -1
    if len(hyp_ids) <= len(ref_ids):
        candidates = (dict(zip(hyp_ids, p)) for p in itertools.permutations(ref_ids, len(hyp_ids)))
    else:
        candidates = (dict(zip(p, ref_ids)) for p in itertools.permutations(hyp_ids, len(ref_ids)))
    for mapping in candidates:
        score = sum(overlap[(h, r)] for h, r in mapping.items())
        if score > best_score:
            best, best_score = mapping, score
    return best


def der(reference, hypothesis, start, end, collar=0.25, step=0.01, mapping=None,
        skip_overlap=False):
    """Diarization error rate with overlapping speech scored and `collar`
    seconds around reference boundaries ignored. Turns are (t0, t1, speaker)
    in seconds. `mapping` (hypothesis -> reference speaker) defaults to the
    optimal one-to-one mapping. `skip_overlap` leaves out frames where the
    reference has several speakers, which a transcript with one speaker per
    segment cannot match. Returns (der, miss, false_alarm, confusion) as
    fractions."""
    if mapping is None:
        mapping = speaker_mapping(reference, hypothesis, start, end, step)
    ref_frames = _frames(reference, start, end, step)
    hyp_frames = _frames(hypothesis, start, end, step)
    ignored = [False] * len(ref_frames)
    for t0, t1, _ in reference:
        for edge in (t0, t1):
            first = max(0, int((edge - collar - start) / step))
            last = min(len(ignored), int((edge + collar - start) / step))
            for i in range(first, last):
                ignored[i] = True
    miss = false_alarm = confusion = total = 0
    for rf, hf, skip in zip(ref_frames, hyp_frames, ignored):
        if skip or (skip_overlap and len(rf) > 1):
            continue
        mapped = {mapping.get(h) for h in hf}
        total += len(rf)
        miss += max(0, len(rf) - len(hf))
        false_alarm += max(0, len(hf) - len(rf))
        confusion += min(len(rf), len(hf)) - len(rf & mapped)
    total = max(total, 1)
    return ((miss + false_alarm + confusion) / total, miss / total,
            false_alarm / total, confusion / total)


# --- word alignment --------------------------------------------------------

def align_words(reference, hypothesis, key):
    """Pairs of (reference word, hypothesis word) that a sequence alignment of
    their normalized texts matches. Words are dicts; `key` normalizes one."""
    ref_keys = [key(w) for w in reference]
    hyp_keys = [key(w) for w in hypothesis]
    matcher = difflib.SequenceMatcher(None, ref_keys, hyp_keys, autojunk=False)
    pairs = []
    for block in matcher.get_matching_blocks():
        for k in range(block.size):
            pairs.append((reference[block.a + k], hypothesis[block.b + k]))
    return pairs


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]
