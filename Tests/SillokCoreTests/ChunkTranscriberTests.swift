import Foundation
import Testing
@testable import SillokCore

private func segment(_ text: String, _ start: TimeInterval, _ end: TimeInterval, logprob: Float = -0.2) -> Segment {
    Segment(start: start, end: end, text: text, language: "", avgLogprob: logprob, noSpeechProb: 0.01, compressionRatio: 1.2)
}

private let chunks = [AudioChunk(start: 0, end: 10), AudioChunk(start: 12, end: 22), AudioChunk(start: 24, end: 34)]
private let source = SilentSource(duration: 40)

@Test func stepCarriesPreviousTextAsPromptWithinSameLanguage() async throws {
    let engine = FakeEngine(detectedLanguage: "ko")
    await engine.enqueue([segment("첫 문장", 0, 5)])
    await engine.enqueue([segment("둘째 문장", 0, 5)])
    let transcriber = ChunkTranscriber(engine: engine, options: TranscriptionOptions(vocabulary: ["GOT"]))

    var state = try await transcriber.step(TranscriptionState(), chunks: chunks, source: source)
    state = try await transcriber.step(state, chunks: chunks, source: source)

    let prompts = await engine.transcriptions.map(\.prompt)
    #expect(prompts == ["GOT", "GOT 첫 문장"])
    #expect(state.nextChunk == 2)
    #expect(state.segments.map(\.start) == [0, 12])
    #expect(state.previousText == "둘째 문장")
}

@Test func languageChangeDropsPreviousText() async throws {
    let engine = FakeEngine(detectedLanguage: "ko")
    await engine.enqueue([segment("한국어", 0, 5)])
    let transcriber = ChunkTranscriber(engine: engine, options: TranscriptionOptions(vocabulary: ["GOT"]))
    let state = try await transcriber.step(TranscriptionState(), chunks: chunks, source: source)

    await engine.setDetectedLanguage("en")
    _ = try await transcriber.step(state, chunks: chunks, source: source)
    #expect(await engine.transcriptions.map(\.prompt) == ["GOT", "GOT"])
    #expect(await engine.transcriptions.map(\.language) == ["ko", "en"])
}

@Test func lowConfidenceChunkResetsPromptContext() async throws {
    let engine = FakeEngine(detectedLanguage: "ko")
    await engine.enqueue([segment("불확실", 0, 5, logprob: -1.5)])
    let transcriber = ChunkTranscriber(engine: engine, options: TranscriptionOptions())
    var state = try await transcriber.step(TranscriptionState(), chunks: chunks, source: source)
    #expect(state.segments.first?.isLowConfidence == true)
    #expect(state.previousText == nil)

    state = try await transcriber.step(state, chunks: chunks, source: source)
    #expect(await engine.transcriptions.map(\.prompt) == [nil, nil])
}

@Test func stateRoundTripsThroughJSON() throws {
    let state = TranscriptionState(nextChunk: 3, previousLanguage: "ko", previousText: "x", segments: [segment("x", 1, 2)])
    let data = try JSONEncoder().encode(state)
    #expect(try JSONDecoder().decode(TranscriptionState.self, from: data) == state)
}
