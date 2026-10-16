import Foundation

public struct SegmentSplitter: Sendable {
    public var maxDuration: TimeInterval = 7
    public var minDuration: TimeInterval = 1.5

    public init() {}

    public func split(_ segments: [Segment]) -> [Segment] {
        segments.flatMap(split)
    }

    func split(_ segment: Segment) -> [Segment] {
        guard let words = segment.words, words.count > 1, segment.end - segment.start > maxDuration else { return [segment] }

        var groups: [ArraySlice<WordTimestamp>] = []
        var start = words.startIndex
        var index = words.startIndex
        while index < words.endIndex {
            let span = words[index].end - words[start].start
            if span > maxDuration, index > start {
                let cut = bestCut(words, from: start, through: index - 1)
                groups.append(words[start...cut])
                start = cut + 1
                continue
            }
            if span >= minDuration, endsSentence(words[index].word), index + 1 < words.endIndex {
                groups.append(words[start...index])
                start = index + 1
            }
            index += 1
        }
        if start < words.endIndex { groups.append(words[start...]) }

        return groups.map { group in
            var part = segment
            part.id = UUID()
            part.words = Array(group)
            part.start = group.first!.start
            part.end = group.last!.end
            part.text = group.map(\.word).joined().trimmingCharacters(in: .whitespaces)
            return part
        }
    }

    private func bestCut(_ words: [WordTimestamp], from start: Int, through last: Int) -> Int {
        let earliest = words[start...last].firstIndex { $0.end - words[start].start >= minDuration } ?? start
        guard earliest < last else { return last }
        if let sentence = (earliest...last).last(where: { endsSentence(words[$0].word) }) { return sentence }
        return (earliest...last).max { gap(words, after: $0) < gap(words, after: $1) }!
    }

    private func gap(_ words: [WordTimestamp], after index: Int) -> TimeInterval {
        index + 1 < words.count ? words[index + 1].start - words[index].end : 0
    }

    private func endsSentence(_ word: String) -> Bool {
        guard let last = word.trimmingCharacters(in: .whitespaces).last else { return false }
        return ".?!。？！".contains(last)
    }
}
