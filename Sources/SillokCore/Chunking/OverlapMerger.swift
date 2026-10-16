import Foundation

public enum OverlapMerger {
    public static func trim(_ segments: [Segment], to chunk: AudioChunk) -> [Segment] {
        removeOverlap(segments.map { $0.clamped(to: chunk.start...chunk.end) }, chunk: chunk)
    }

    private static func removeOverlap(_ segments: [Segment], chunk: AudioChunk) -> [Segment] {
        guard chunk.cutBefore != nil || chunk.cutAfter != nil else { return segments }
        let keeps: (TimeInterval) -> Bool = { mid in
            if let cut = chunk.cutBefore, mid < cut { return false }
            if let cut = chunk.cutAfter, mid >= cut { return false }
            return true
        }
        return segments.compactMap { segment in
            guard let words = segment.words, !words.isEmpty else {
                return keeps((segment.start + segment.end) / 2) ? segment : nil
            }
            let kept = words.filter { keeps(($0.start + $0.end) / 2) }
            guard let first = kept.first, let last = kept.last else { return nil }
            guard kept.count != words.count else { return segment }
            var trimmed = segment
            trimmed.words = kept
            trimmed.start = first.start
            trimmed.end = last.end
            trimmed.text = kept.map(\.word).joined().trimmingCharacters(in: .whitespaces)
            return trimmed
        }
    }
}

extension Segment {
    func clamped(to range: ClosedRange<TimeInterval>) -> Segment {
        var clamped = self
        clamped.start = start.clamped(to: range)
        clamped.end = end.clamped(to: range)
        clamped.words = words?.map {
            WordTimestamp(word: $0.word, start: $0.start.clamped(to: range), end: $0.end.clamped(to: range), probability: $0.probability)
        }
        return clamped
    }

    public func offset(by delta: TimeInterval) -> Segment {
        var shifted = self
        shifted.start += delta
        shifted.end += delta
        shifted.words = words?.map {
            WordTimestamp(word: $0.word, start: $0.start + delta, end: $0.end + delta, probability: $0.probability)
        }
        return shifted
    }
}

extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}
