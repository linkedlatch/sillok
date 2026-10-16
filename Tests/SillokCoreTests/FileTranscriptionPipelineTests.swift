import Foundation
import Testing
@testable import SillokCore

private actor EchoEngine: TranscriptionEngine {
    var calls = 0
    func loadModel(_ files: ModelFiles, mode: ComputeMode) async throws {}
    func unloadModel() async {}
    func detectLanguage(samples: [Float], allowedLanguages: Set<String>?) async throws -> LanguageDetection {
        LanguageDetection(language: "ko", confidence: 1)
    }
    func tokenCount(_ text: String) async throws -> Int { text.split(separator: " ").count }
    func transcribe(samples: [Float], options: ChunkTranscribeOptions) async throws -> [Segment] {
        calls += 1
        let text = "n\(samples.count) p[\(options.prompt ?? "")]"
        return [Segment(start: 1, end: 2, text: text, language: options.language, avgLogprob: -0.1, noSpeechProb: 0, compressionRatio: 1)]
    }
}

private struct AlwaysSpeech: VoiceActivityDetector {
    let frameSize = 512
    func reset() async {}
    func speechProbabilities(_ samples: [Float]) async throws -> [Float] {
        [Float](repeating: 0.9, count: (samples.count + frameSize - 1) / frameSize)
    }
}

private struct HotDevice: ThermalStateProvider {
    var level: ThermalLevel { .serious }
}

private func pipeline(_ engine: EchoEngine, checkpoint: URL) -> FileTranscriptionPipeline {
    FileTranscriptionPipeline(
        engine: engine, vad: AlwaysSpeech(),
        options: TranscriptionOptions(languageMode: .fixed("ko"), vocabulary: ["GOT"]),
        modelID: "test", checkpointURL: checkpoint
    )
}

private func checkpointURL() -> URL {
    FileManager.default.temporaryDirectory.appendingPathComponent("sillok-cp-\(UUID().uuidString)/checkpoint.json")
}

private func collect(_ stream: AsyncThrowingStream<TranscribeProgress, Error>, stopAfter limit: Int? = nil) async throws -> [Segment] {
    var segments: [Segment] = []
    var count = 0
    for try await progress in stream {
        segments += progress.newSegments
        count += 1
        if let limit, count >= limit { break }
    }
    return segments
}

private func summary(_ segments: [Segment]) -> [String] {
    segments.map { "\($0.start)-\($0.end) \($0.text)" }
}

@Test func resumedRunMatchesUninterruptedRun() async throws {
    let source = SilentSource(duration: 100)
    let reference = try await collect(pipeline(EchoEngine(), checkpoint: checkpointURL()).run(source: source))
    #expect(reference.count == 4)

    let url = checkpointURL()
    let partial = try await collect(pipeline(EchoEngine(), checkpoint: url).run(source: source), stopAfter: 3)
    #expect(partial.count == 2)
    let saved = try VersionedStore.load(Checkpoint.self, from: url)
    #expect(saved.state.nextChunk >= 2)

    let resumedEngine = EchoEngine()
    let resumed = try await collect(pipeline(resumedEngine, checkpoint: url).run(source: source))
    #expect(summary(resumed) == summary(reference))
    #expect(await resumedEngine.calls == 4 - saved.state.nextChunk)
}

@Test func checkpointWithDifferentOptionsIsIgnored() async throws {
    let source = SilentSource(duration: 100)
    let url = checkpointURL()
    _ = try await collect(pipeline(EchoEngine(), checkpoint: url).run(source: source), stopAfter: 3)

    var other = pipeline(EchoEngine(), checkpoint: url)
    other.options.vocabulary = ["다른 용어"]
    let segments = try await collect(other.run(source: source))
    #expect(segments.count == 4)
    #expect(segments.allSatisfy { $0.text.contains("다른 용어") })
}

@Test func progressReachesCompletion() async throws {
    var fractions: [Double] = []
    for try await progress in pipeline(EchoEngine(), checkpoint: checkpointURL()).run(source: SilentSource(duration: 100)) {
        fractions.append(progress.fractionCompleted)
    }
    #expect(fractions.first == 0)
    #expect(fractions.last == 1)
    #expect(fractions == fractions.sorted())
}

@Test func hotDeviceWaitsBetweenChunks() async throws {
    var hot = pipeline(EchoEngine(), checkpoint: checkpointURL())
    hot.thermal = HotDevice()
    hot.cooldown = .milliseconds(100)
    let clock = ContinuousClock()
    let elapsed = try await clock.measure { _ = try await collect(hot.run(source: SilentSource(duration: 100))) }
    #expect(elapsed >= .milliseconds(300))
}
