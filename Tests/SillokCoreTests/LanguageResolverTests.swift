import Foundation
import Testing
@testable import SillokCore

private func samples(_ seconds: TimeInterval) -> [Float] {
    [Float](repeating: 0, count: Int(seconds * Double(PCMFormat.sampleRate)))
}

@Test func fixedModeNeverDetects() async throws {
    let engine = FakeEngine()
    let chunks = [AudioChunk(start: 0, end: 10)]
    let language = try await LanguageResolver(mode: .fixed("ja")).resolve(
        chunkIndex: 0, samples: samples(10), chunks: chunks, previousLanguage: nil,
        source: SilentSource(duration: 10), engine: engine)
    #expect(language == "ja")
    #expect(await engine.detections.isEmpty)
}

@Test(arguments: [LanguageMode.auto, LanguageMode.koreanEnglish])
func longChunkIsDetectedWithModeCandidates(mode: LanguageMode) async throws {
    let allowed: Set<String>? = mode == .auto ? nil : ["ko", "en"]
    let engine = FakeEngine(detectedLanguage: "en")
    let chunks = [AudioChunk(start: 0, end: 10)]
    let language = try await LanguageResolver(mode: mode).resolve(
        chunkIndex: 0, samples: samples(10), chunks: chunks, previousLanguage: "ko",
        source: SilentSource(duration: 10), engine: engine)
    #expect(language == "en")
    #expect(await engine.detections.map(\.allowed) == [allowed])
}

@Test func shortChunkInheritsPreviousLanguage() async throws {
    let engine = FakeEngine(detectedLanguage: "en")
    let chunks = [AudioChunk(start: 0, end: 10), AudioChunk(start: 12, end: 14)]
    let language = try await LanguageResolver(mode: .auto).resolve(
        chunkIndex: 1, samples: samples(2), chunks: chunks, previousLanguage: "ko",
        source: SilentSource(duration: 20), engine: engine)
    #expect(language == "ko")
    #expect(await engine.detections.isEmpty)
}

@Test func firstShortChunkIsDetectedTogetherWithFollowingChunks() async throws {
    let engine = FakeEngine(detectedLanguage: "en")
    let chunks = [AudioChunk(start: 0, end: 1), AudioChunk(start: 2, end: 3.5), AudioChunk(start: 5, end: 15)]
    let language = try await LanguageResolver(mode: .auto).resolve(
        chunkIndex: 0, samples: samples(1), chunks: chunks, previousLanguage: nil,
        source: SilentSource(duration: 20), engine: engine)
    #expect(language == "en")
    let detections = await engine.detections
    #expect(detections.count == 1)
    #expect(detections[0].sampleCount == Int(12.5 * Double(PCMFormat.sampleRate)))
}

@Test func audioShorterThanThreeSecondsFallsBackToKorean() async throws {
    let engine = FakeEngine(detectedLanguage: "en")
    let chunks = [AudioChunk(start: 0, end: 1), AudioChunk(start: 1.5, end: 2.5)]
    let language = try await LanguageResolver(mode: .koreanEnglish).resolve(
        chunkIndex: 0, samples: samples(1), chunks: chunks, previousLanguage: nil,
        source: SilentSource(duration: 2.5), engine: engine)
    #expect(language == "ko")
    #expect(await engine.detections.isEmpty)
}

private let threeChunks = [AudioChunk(start: 0, end: 10), AudioChunk(start: 12, end: 22), AudioChunk(start: 24, end: 34)]

private func resolveMiddle(_ scripted: [LanguageDetection], previous: String? = "ko") async throws -> (String, Int) {
    let engine = FakeEngine()
    await engine.script(scripted)
    let language = try await LanguageResolver(mode: .koreanEnglish).resolve(
        chunkIndex: 1, samples: samples(10), chunks: threeChunks, previousLanguage: previous,
        source: SilentSource(duration: 40), engine: engine)
    return (language, await engine.detections.count)
}

@Test func lowConfidenceSwitchKeepsPreviousLanguage() async throws {
    let (language, detections) = try await resolveMiddle([LanguageDetection(language: "en", confidence: 0.7)])
    #expect(language == "ko")
    #expect(detections == 1)
}

@Test func confidentSwitchConfirmedByNextChunkSwitches() async throws {
    let (language, detections) = try await resolveMiddle([
        LanguageDetection(language: "en", confidence: 0.9),
        LanguageDetection(language: "en", confidence: 0.85),
    ])
    #expect(language == "en")
    #expect(detections == 2)
}

@Test(arguments: [LanguageDetection(language: "ko", confidence: 0.9), LanguageDetection(language: "en", confidence: 0.6)])
func confidentSwitchNotConfirmedKeepsPreviousLanguage(next: LanguageDetection) async throws {
    let (language, _) = try await resolveMiddle([LanguageDetection(language: "en", confidence: 0.95), next])
    #expect(language == "ko")
}

@Test func firstChunkAcceptsDetectionWithoutConfirmation() async throws {
    let (language, detections) = try await resolveMiddle([LanguageDetection(language: "en", confidence: 0.55)], previous: nil)
    #expect(language == "en")
    #expect(detections == 1)
}

@Test func sameLanguageNeedsNoConfirmation() async throws {
    let (language, detections) = try await resolveMiddle([LanguageDetection(language: "ko", confidence: 0.51)])
    #expect(language == "ko")
    #expect(detections == 1)
}
