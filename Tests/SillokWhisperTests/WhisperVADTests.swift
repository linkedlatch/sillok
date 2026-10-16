import Foundation
import Testing
import SillokCore
@testable import SillokWhisper

private struct ArraySource: AudioStreamSource {
    let samples: [Float]
    var duration: TimeInterval { Double(samples.count) / Double(PCMFormat.sampleRate) }
    func read(from start: TimeInterval, to end: TimeInterval) async throws -> [Float] {
        let lower = max(Int((start * Double(PCMFormat.sampleRate)).rounded()), 0)
        let upper = min(Int((end * Double(PCMFormat.sampleRate)).rounded()), samples.count)
        return lower < upper ? Array(samples[lower..<upper]) : []
    }
}

@Suite(.serialized, .enabled(if: TestSupport.hasVADModel))
struct WhisperVADTests {
    @Test func speechBetweenSilencesBecomesOneChunk() async throws {
        let speech = try await TestSupport.speech("The quick brown fox jumps over the lazy dog.", voice: "Samantha")
        let silence = [Float](repeating: 0, count: 3 * PCMFormat.sampleRate)
        let source = ArraySource(samples: silence + speech + silence)
        let vad = try WhisperVAD(modelURL: TestSupport.vadModel)

        let map = try await SpeechMap.build(source: source, vad: vad, framesPerWindow: 50)
        #expect(map.probabilities.count == (source.samples.count + 511) / 512)
        #expect(map.energies.count == map.probabilities.count)

        let chunks = ChunkPlanner().plan(map)
        let speechEnd = 3 + Double(speech.count) / Double(PCMFormat.sampleRate)
        #expect(chunks.count == 1)
        #expect(abs(chunks[0].start - 3) < 0.5)
        #expect(abs(chunks[0].end - speechEnd) < 0.7)
    }
}

@Suite(.serialized, .enabled(if: TestSupport.hasTinyModel))
struct WordTimestampTests {
    @Test func transcriptionIncludesOrderedWordsMatchingText() async throws {
        let samples = try await TestSupport.speech("The quick brown fox jumps over the lazy dog.", voice: "Samantha")
        let engine = WhisperCppEngine()
        try await engine.loadModel(ModelFiles(ggml: TestSupport.tinyModel), mode: .foregroundAccelerated)
        let segments = try await engine.transcribe(samples: samples, options: ChunkTranscribeOptions(language: "en"))
        let duration = Double(samples.count) / Double(PCMFormat.sampleRate)

        for segment in segments {
            let words = try #require(segment.words)
            #expect(words.map(\.word).joined().trimmingCharacters(in: .whitespaces) == segment.text)
            for pair in zip(words, words.dropFirst()) {
                #expect(pair.0.start <= pair.1.start)
            }
            #expect(words.allSatisfy { $0.start >= 0 && $0.end <= duration + 0.5 && $0.start <= $0.end })
        }
    }
}
