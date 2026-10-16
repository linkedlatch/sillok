import Foundation
import Testing
@testable import SillokCore

private let frame: TimeInterval = 0.1

private func map(duration: TimeInterval, speech: [ClosedRange<TimeInterval>], quiet: [TimeInterval] = []) -> SpeechMap {
    let count = Int((duration / frame).rounded())
    var probabilities = [Float](repeating: 0.02, count: count)
    var energies = [Float](repeating: 0.001, count: count)
    for range in speech {
        for i in Int((range.lowerBound / frame).rounded())..<Int((range.upperBound / frame).rounded()) {
            probabilities[i] = 0.9
            energies[i] = 0.2
        }
    }
    for time in quiet {
        energies[Int(time / frame)] = 0.01
    }
    return SpeechMap(frameDuration: frame, probabilities: probabilities, energies: energies, duration: duration)
}

private func approx(_ a: TimeInterval?, _ b: TimeInterval?) -> Bool {
    guard let a, let b else { return a == nil && b == nil }
    return abs(a - b) < 1e-6
}

@Test func silenceOnlyProducesNoChunks() {
    #expect(ChunkPlanner().plan(map(duration: 60, speech: [])).isEmpty)
}

@Test func leadingAndTrailingSilenceAreExcludedWithPadding() {
    let chunks = ChunkPlanner().plan(map(duration: 20, speech: [5...8]))
    #expect(chunks.count == 1)
    #expect(approx(chunks[0].start, 4.8))
    #expect(approx(chunks[0].end, 8.2))
    #expect(chunks[0].cutBefore == nil && chunks[0].cutAfter == nil)
}

@Test func regionsWithinThirtySecondsShareOneChunk() {
    let chunks = ChunkPlanner().plan(map(duration: 40, speech: [1...10, 12...20, 22...28]))
    #expect(chunks.count == 1)
    #expect(approx(chunks[0].start, 0.8))
    #expect(approx(chunks[0].end, 28.2))
}

@Test func halfSecondSilenceIsASplitPointWhenChunkWouldExceedThirtySeconds() {
    let chunks = ChunkPlanner().plan(map(duration: 40, speech: [0...20, 20.6...35]))
    #expect(chunks.count == 2)
    #expect(approx(chunks[0].start, 0) && approx(chunks[0].end, 20.2))
    #expect(approx(chunks[1].start, 20.4) && approx(chunks[1].end, 35.2))
    #expect(chunks.allSatisfy { $0.cutBefore == nil && $0.cutAfter == nil })
}

@Test func silenceShorterThanHalfSecondIsNotASplitPoint() {
    let chunks = ChunkPlanner().plan(map(duration: 40, speech: [0...20, 20.3...35], quiet: [27.05]))
    #expect(chunks.count == 2)
    #expect(approx(chunks[0].cutAfter, 27.05))
}

@Test func continuousSpeechIsCutAtQuietestPointBetween25And30WithOverlap() {
    let chunks = ChunkPlanner().plan(map(duration: 80, speech: [0...70], quiet: [27.05, 53.55]))
    #expect(chunks.count == 3)

    #expect(approx(chunks[0].start, 0))
    #expect(approx(chunks[0].cutAfter, 27.05))
    #expect(approx(chunks[0].end, 27.55))

    #expect(approx(chunks[1].start, 26.55))
    #expect(approx(chunks[1].cutBefore, 27.05))
    #expect(approx(chunks[1].cutAfter, 53.55))
    #expect(approx(chunks[1].end, 54.05))

    #expect(approx(chunks[2].start, 53.05))
    #expect(approx(chunks[2].cutBefore, 53.55))
    #expect(chunks[2].cutAfter == nil)
    #expect(approx(chunks[2].end, 70.2))
}

@Test func forcedCutStaysInsideSearchWindowWithoutQuietFrames() {
    let chunks = ChunkPlanner().plan(map(duration: 100, speech: [0...95]))
    for pair in zip(chunks, chunks.dropFirst()) {
        let cut = try! #require(pair.0.cutAfter)
        #expect(cut >= pair.0.start + 25 && cut <= pair.0.start + 29.5)
        #expect(approx(pair.1.cutBefore, cut))
        #expect(approx(pair.1.start, cut - 0.5))
        #expect(approx(pair.0.end, cut + 0.5))
    }
}

@Test(arguments: 0..<20)
func everyChunkIsAtMostThirtySecondsAndCoversAllSpeech(seed: Int) {
    var generator = SeededGenerator(seed: UInt64(seed + 1))
    var probabilities: [Float] = []
    var energies: [Float] = []
    while probabilities.count < 6000 {
        let speech = Bool.random(using: &generator)
        let length = speech ? Int.random(in: 1...500, using: &generator) : Int.random(in: 1...30, using: &generator)
        for _ in 0..<length {
            probabilities.append(speech ? 0.9 : 0.05)
            energies.append(Float.random(in: 0...1, using: &generator))
        }
    }
    let speechMap = SpeechMap(frameDuration: frame, probabilities: probabilities, energies: energies, duration: Double(probabilities.count) * frame)
    let chunks = ChunkPlanner().plan(speechMap)

    #expect(chunks.allSatisfy { $0.duration <= 30 + 1e-9 })
    for (i, p) in probabilities.enumerated() where p >= 0.5 {
        let t = (Double(i) + 0.5) * frame
        #expect(chunks.contains { t >= $0.start && t <= $0.end })
    }
}

struct SeededGenerator: RandomNumberGenerator {
    var state: UInt64
    init(seed: UInt64) { state = seed }
    mutating func next() -> UInt64 {
        state = state &* 6364136223846793005 &+ 1442695040888963407
        return state
    }
}
