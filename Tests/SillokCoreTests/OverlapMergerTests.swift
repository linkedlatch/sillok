import Foundation
import Testing
@testable import SillokCore

private func word(_ text: String, _ start: TimeInterval, _ end: TimeInterval) -> WordTimestamp {
    WordTimestamp(word: text, start: start, end: end, probability: 0.9)
}

private func segment(_ words: [WordTimestamp]) -> Segment {
    Segment(
        start: words.first!.start, end: words.last!.end,
        text: words.map(\.word).joined().trimmingCharacters(in: .whitespaces),
        language: "en", words: words
    )
}

@Test func wordsAreAssignedByMidpointAroundCut() {
    let cut: TimeInterval = 27
    let previous = [segment([word(" one", 25.0, 25.8), word(" two", 26.5, 26.9), word(" three", 26.8, 27.4), word(" four", 27.1, 27.5)])]
    let next = [segment([word(" two", 26.6, 26.9), word(" three", 26.7, 27.3), word(" four", 27.1, 27.4), word(" five", 27.6, 28.0)])]

    let kept = OverlapMerger.trim(previous, to: AudioChunk(start: 0, end: 27.5, cutAfter: cut))
        + OverlapMerger.trim(next, to: AudioChunk(start: 26.5, end: 40, cutBefore: cut))

    #expect(kept.map(\.text) == ["one two", "three four five"])
    #expect(kept[0].end == 26.9)
    #expect(kept[1].start == 26.7)
}

@Test func wordWithMidpointExactlyAtCutGoesToLaterChunk() {
    let cut: TimeInterval = 10
    let boundary = [segment([word(" a", 8, 9), word(" b", 9.8, 10.2)])]
    #expect(OverlapMerger.trim(boundary, to: AudioChunk(start: 0, end: 10.5, cutAfter: cut)).map(\.text) == ["a"])
    #expect(OverlapMerger.trim(boundary, to: AudioChunk(start: 9.5, end: 20, cutBefore: cut)).map(\.text) == ["b"])
}

@Test func segmentWithoutWordsUsesSegmentMidpoint() {
    let segments = [
        Segment(start: 9, end: 10.8, text: "early", language: "ko"),
        Segment(start: 9.6, end: 10.8, text: "late", language: "ko"),
    ]
    #expect(OverlapMerger.trim(segments, to: AudioChunk(start: 0, end: 10.5, cutAfter: 10)).map(\.text) == ["early"])
}

@Test func chunkWithoutCutsIsUnchanged() {
    let segments = [segment([word(" x", 1, 2)])]
    #expect(OverlapMerger.trim(segments, to: AudioChunk(start: 0, end: 5)) == segments)
}

@Test func offsetShiftsSegmentAndWords() {
    let shifted = segment([word(" x", 1, 2)]).offset(by: 30)
    #expect(shifted.start == 31 && shifted.end == 32)
    #expect(shifted.words?.first?.start == 31)
}

@Test func segmentAndWordTimesAreClampedToChunk() {
    let overshooting = [segment([word(" a", 96, 97), word(" b", 97.5, 99.02)])]
    let trimmed = OverlapMerger.trim(overshooting, to: AudioChunk(start: 70.56, end: 98.57))
    #expect(trimmed[0].end == 98.57)
    #expect(trimmed[0].words?.last?.end == 98.57)
    #expect(trimmed[0].text == "a b")
}
