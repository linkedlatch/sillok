import Foundation
import Testing
import SillokCore
@testable import SillokWhisper

private let korean = "오늘 강의에서는 포맷 스트링 버그가 어떻게 발생하는지 살펴보고, 해커가 이 취약점을 이용해 메모리의 값을 덮어쓰는 과정을 차근차근 설명하겠습니다. 마지막으로 실제 사례를 통해 개발자가 주의해야 할 점을 정리하겠습니다."
private let english = "In today's lecture we will look at how format string bugs happen, and explain step by step how an attacker can use this vulnerability to overwrite values in memory. Finally, we will summarize what developers should watch out for, using a real world example."

private struct ArraySource: AudioStreamSource {
    let samples: [Float]
    var duration: TimeInterval { Double(samples.count) / Double(PCMFormat.sampleRate) }
    func read(from start: TimeInterval, to end: TimeInterval) async throws -> [Float] {
        let lower = max(Int((start * Double(PCMFormat.sampleRate)).rounded()), 0)
        let upper = min(Int((end * Double(PCMFormat.sampleRate)).rounded()), samples.count)
        return lower < upper ? Array(samples[lower..<upper]) : []
    }
}

@Test func compressionRatioFlagsRepetition() throws {
    let normal = try #require(CompressionRatio.of(english))
    let repeated = try #require(CompressionRatio.of(String(repeating: "감사합니다. ", count: 40)))
    #expect(normal < 2.4)
    #expect(repeated > 2.4)
    #expect(CompressionRatio.of("") == nil)
}

@Suite(.serialized, .enabled(if: TestSupport.hasTinyModel))
struct LanguageDetectionTests {
    let engine = WhisperCppEngine()

    @Test(arguments: [("Yuna", "ko"), ("Samantha", "en")])
    func detectsSpokenLanguage(voice: String, expected: String) async throws {
        try await engine.loadModel(ModelFiles(ggml: TestSupport.tinyModel), mode: .foregroundAccelerated)
        let samples = try await TestSupport.speech(expected == "ko" ? korean : english, voice: voice)

        let any = try await engine.detectLanguage(samples: samples, allowedLanguages: nil)
        #expect(any.language == expected)
        #expect(any.confidence > 0.5 && any.confidence <= 1)

        let restricted = try await engine.detectLanguage(samples: samples, allowedLanguages: ["ko", "en"])
        #expect(restricted.language == expected)
        #expect(restricted.confidence >= any.confidence)
    }

    @Test func tokenCountGrowsWithText() async throws {
        try await engine.loadModel(ModelFiles(ggml: TestSupport.tinyModel), mode: .foregroundAccelerated)
        let short = try await engine.tokenCount("hello")
        let long = try await engine.tokenCount(english)
        #expect(short > 0 && long > short)
    }
}

@Suite(.serialized, .enabled(if: TestSupport.hasTinyModel && TestSupport.hasVADModel))
struct MixedLanguagePipelineTests {
    @Test func koreanThenEnglishSwitchesLanguagePerChunk() async throws {
        let ko = try await TestSupport.speech(korean, voice: "Yuna")
        let en = try await TestSupport.speech(english, voice: "Samantha")
        let gap = [Float](repeating: 0, count: PCMFormat.sampleRate)
        let source = ArraySource(samples: ko + gap + en)

        let map = try await SpeechMap.build(source: source, vad: try WhisperVAD(modelURL: TestSupport.vadModel))
        let chunks = ChunkPlanner().plan(map)
        try #require(chunks.count >= 2)

        let engine = WhisperCppEngine()
        try await engine.loadModel(ModelFiles(ggml: TestSupport.tinyModel), mode: .foregroundAccelerated)
        let transcriber = ChunkTranscriber(engine: engine, options: TranscriptionOptions(languageMode: .koreanEnglish, beamSize: 1))
        var state = TranscriptionState()
        while !state.isComplete(chunkCount: chunks.count) {
            state = try await transcriber.step(state, chunks: chunks, source: source)
        }

        let koEnd = Double(ko.count) / Double(PCMFormat.sampleRate)
        let firstHalf = state.segments.filter { $0.end <= koEnd + 0.5 }
        let secondHalf = state.segments.filter { $0.start >= koEnd + 0.5 }
        #expect(!firstHalf.isEmpty && firstHalf.allSatisfy { $0.language == "ko" })
        #expect(!secondHalf.isEmpty && secondHalf.allSatisfy { $0.language == "en" })
        #expect(secondHalf.map(\.text).joined(separator: " ").lowercased().contains("format"))
    }
}
