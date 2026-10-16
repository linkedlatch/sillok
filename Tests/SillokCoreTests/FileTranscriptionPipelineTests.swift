import Foundation
import Testing
@testable import SillokCore

private struct SilentSource: AudioStreamSource {
    var duration: TimeInterval

    func read(from start: TimeInterval, to end: TimeInterval) throws -> [Float] {
        let count = Int(((min(end, duration) - max(start, 0)) * Double(PCMFormat.sampleRate)).rounded())
        return [Float](repeating: 0, count: max(count, 0))
    }
}

private actor EchoCore: TranscriptionCore {
    var steps = 0

    nonisolated func plan(source: AudioStreamSource) async throws -> [AudioChunk] {
        stride(from: 0.0, to: source.duration, by: 25).map { AudioChunk(start: $0, end: min($0 + 25, source.duration)) }
    }

    func step(_ state: TranscriptionState, chunks: [AudioChunk], source: AudioStreamSource, options: TranscriptionOptions) async throws -> TranscriptionState {
        steps += 1
        let chunk = chunks[state.nextChunk]
        var next = state
        next.nextChunk += 1
        next.previousText = "p[\(state.previousText ?? "")] n\(state.nextChunk)"
        next.segments.append(Segment(start: chunk.start, end: chunk.end, text: next.previousText!, language: "ko"))
        return next
    }
}

private struct HotDevice: ThermalStateProvider {
    var level: ThermalLevel { .serious }
}

private func pipeline(_ core: EchoCore, checkpoint: URL) -> FileTranscriptionPipeline {
    FileTranscriptionPipeline(core: core, options: TranscriptionOptions(languageMode: .fixed("ko"), vocabulary: ["GOT"]), modelID: "test", checkpointURL: checkpoint)
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
    let reference = try await collect(pipeline(EchoCore(), checkpoint: checkpointURL()).run(source: source))
    #expect(reference.count == 4)

    let url = checkpointURL()
    let partial = try await collect(pipeline(EchoCore(), checkpoint: url).run(source: source), stopAfter: 3)
    #expect(partial.count == 2)

    let resumedCore = EchoCore()
    var resumed: [Segment] = []
    var alreadyDone: Int?
    for try await progress in pipeline(resumedCore, checkpoint: url).run(source: source) {
        if alreadyDone == nil { alreadyDone = progress.newSegments.count }
        resumed += progress.newSegments
    }
    #expect((alreadyDone ?? 0) >= 2)
    #expect(summary(resumed) == summary(reference))
    #expect(await resumedCore.steps == 4 - (alreadyDone ?? 0))
}

@Test func checkpointWithDifferentOptionsIsIgnored() async throws {
    let source = SilentSource(duration: 100)
    let url = checkpointURL()
    _ = try await collect(pipeline(EchoCore(), checkpoint: url).run(source: source), stopAfter: 3)

    let core = EchoCore()
    var other = pipeline(core, checkpoint: url)
    other.options.vocabulary = ["다른 용어"]
    let segments = try await collect(other.run(source: source))
    #expect(segments.count == 4)
    #expect(await core.steps == 4)
}

@Test func progressReachesCompletion() async throws {
    var fractions: [Double] = []
    for try await progress in pipeline(EchoCore(), checkpoint: checkpointURL()).run(source: SilentSource(duration: 100)) {
        fractions.append(progress.fractionCompleted)
    }
    #expect(fractions.first == 0)
    #expect(fractions.last == 1)
    #expect(fractions == fractions.sorted())
}

@Test func hotDeviceWaitsBetweenChunks() async throws {
    var hot = pipeline(EchoCore(), checkpoint: checkpointURL())
    hot.thermal = HotDevice()
    hot.cooldown = .milliseconds(100)
    let clock = ContinuousClock()
    let elapsed = try await clock.measure { _ = try await collect(hot.run(source: SilentSource(duration: 100))) }
    #expect(elapsed >= .milliseconds(300))
}
