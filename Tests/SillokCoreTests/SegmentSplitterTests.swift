import Foundation
import Testing
@testable import SillokCore

private func words(_ items: [(String, TimeInterval, TimeInterval)]) -> [WordTimestamp] {
    items.map { WordTimestamp(word: $0.0, start: $0.1, end: $0.2, probability: 0.9) }
}

private func segment(_ words: [WordTimestamp]) -> Segment {
    Segment(start: words.first!.start, end: words.last!.end,
            text: words.map(\.word).joined().trimmingCharacters(in: .whitespaces),
            language: "ko", words: words, avgLogprob: -0.3)
}

@Test func shortSegmentIsUnchanged() {
    let original = segment(words([(" 짧은", 0, 1), (" 문장.", 1, 2), (" 끝", 2, 3)]))
    #expect(SegmentSplitter().split([original]) == [original])
}

@Test func longSegmentSplitsAtSentenceEnds() {
    let parts = SegmentSplitter().split([segment(words([
        (" 첫", 0, 1), (" 문장입니다.", 1, 2.5), (" 둘째", 2.6, 4), (" 문장도", 4, 5.5), (" 있습니다.", 5.5, 7), (" 셋째", 7.1, 8), (" 끝", 8, 9),
    ]))])
    #expect(parts.map(\.text) == ["첫 문장입니다.", "둘째 문장도 있습니다.", "셋째 끝"])
    #expect(parts.map(\.start) == [0, 2.6, 7.1])
    #expect(parts.allSatisfy { $0.avgLogprob == -0.3 && $0.language == "ko" })
    #expect(Set(parts.map(\.id)).count == 3)
}

@Test func unpunctuatedSegmentSplitsAtLargestPause() {
    let items = (0..<12).map { i -> (String, TimeInterval, TimeInterval) in
        let start = Double(i) + (i >= 5 ? 0.8 : 0)
        return (" 단어\(i)", start, start + 0.9)
    }
    let parts = SegmentSplitter().split([segment(words(items))])
    #expect(parts.count >= 2)
    #expect(parts[0].words?.last?.word == " 단어4")
    #expect(parts.allSatisfy { $0.end - $0.start <= 7 + 1e-9 })
    #expect(parts.flatMap { $0.words ?? [] }.count == 12)
}

@Test(arguments: 0..<10)
func everyPartRespectsMaxDurationAndKeepsAllWords(seed: Int) {
    var generator = SeededGenerator(seed: UInt64(seed + 11))
    var time = 0.0
    var items: [(String, TimeInterval, TimeInterval)] = []
    for i in 0..<80 {
        let length = Double.random(in: 0.1...1.2, using: &generator)
        let punctuation = Int.random(in: 0..<8, using: &generator) == 0 ? "." : ""
        items.append((" w\(i)\(punctuation)", time, time + length))
        time += length + Double.random(in: 0...0.6, using: &generator)
    }
    let parts = SegmentSplitter().split([segment(words(items))])
    #expect(parts.allSatisfy { $0.end - $0.start <= 7 + 1e-9 || ($0.words?.count ?? 0) == 1 })
    #expect(parts.flatMap { $0.words ?? [] }.map(\.word) == items.map(\.0))
}
