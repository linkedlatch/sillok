import Foundation
import Testing
import SillokAudio
import SillokCore
@testable import SillokNative

private let repoRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let model = repoRoot.appendingPathComponent("models/ggml-large-v3-turbo-q5_0.bin")
private let vad = repoRoot.appendingPathComponent("models/ggml-silero-v6.2.0.bin")

private func fixture(_ name: String) -> URL {
    repoRoot.appendingPathComponent("build/fixtures/\(name)")
}

private func exists(_ urls: URL...) -> Bool {
    urls.allSatisfy { FileManager.default.fileExists(atPath: $0.path) }
}

private func samples(_ name: String) async throws -> [Float] {
    let source = try await AssetAudioSource(url: fixture(name))
    return try await source.readAsync(from: 0, to: source.duration)
}

private struct ArraySource: AudioStreamSource {
    let samples: [Float]
    var duration: TimeInterval { Double(samples.count) / Double(PCMFormat.sampleRate) }

    func read(from start: TimeInterval, to end: TimeInterval) throws -> [Float] {
        let lower = max(Int((start * Double(PCMFormat.sampleRate)).rounded()), 0)
        let upper = min(Int((end * Double(PCMFormat.sampleRate)).rounded()), samples.count)
        return lower < upper ? Array(samples[lower..<upper]) : []
    }
}

@Test func buildIncludesMetalAndCoreML() {
    let info = NativeEngine.systemInfo
    #expect(info.contains("MTL"))
    #expect(info.contains("COREML = 1"))
}

@Test func exportUsesNativeFormatter() throws {
    let segments = [Segment(start: 0.5, end: 2.25, text: "안녕하세요", language: "ko")]
    #expect(try NativeExport.render(segments, as: .srt) == "1\n00:00:00,500 --> 00:00:02,250\n안녕하세요\n")
    #expect(try NativeExport.render(segments, as: .txt) == "안녕하세요\n")
}

@Test func missingModelReportsError() async {
    await #expect(throws: NativeError.self) {
        try await NativeEngine().load(ModelFiles(ggml: URL(fileURLWithPath: "/nonexistent/ggml.bin")), mode: .foregroundAccelerated)
    }
}

@Suite(.serialized, .enabled(if: exists(model, vad, fixture("en.wav"), fixture("ko.wav"), fixture("ko-long.wav"), fixture("en-long.wav"))))
struct TurboTests {
    static let engine = NativeEngine()

    init() async throws {
        if Self.engine.loadedFiles == nil {
            try await Self.engine.load(ModelFiles(ggml: model), mode: .foregroundAccelerated)
        }
    }

    @Test func transcribesEnglishSpeechWithWords() async throws {
        let segments = try await Self.engine.transcribe(samples: try await samples("en.wav"), options: ChunkTranscribeOptions(language: "en"))
        let text = segments.map(\.text).joined(separator: " ").lowercased()
        #expect(text.contains("fox") && text.contains("dog"))
        #expect(segments.allSatisfy { $0.words?.isEmpty == false && $0.avgLogprob != nil && $0.language == "en" })
        #expect(Set(segments.map(\.id)).count == segments.count)
    }

    @Test(arguments: [("ko.wav", "ko"), ("en.wav", "en")])
    func detectsLanguage(file: String, expected: String) async throws {
        let audio = try await samples(file)
        let any = try await Self.engine.detectLanguage(samples: audio, allowedLanguages: nil)
        let restricted = try await Self.engine.detectLanguage(samples: audio, allowedLanguages: ["ko", "en"])
        #expect(any.language == expected)
        #expect(restricted.language == expected)
        #expect(restricted.confidence >= any.confidence)
    }

    @Test func taskCancellationStopsWithinOneEncoderPass() async throws {
        let sentence = try await samples("en.wav")
        let long = Array(repeatElement(sentence, count: 30).joined())
        let task = Task { try await Self.engine.transcribe(samples: long, options: ChunkTranscribeOptions(language: "en")) }
        try await Task.sleep(for: .milliseconds(500))
        let clock = ContinuousClock()
        let cancelledAt = clock.now
        task.cancel()
        let result = await task.result
        #expect(throws: CancellationError.self) { try result.get() }
        #expect(clock.now - cancelledAt < .seconds(3))
    }

    @Test func pipelineSwitchesFromKoreanToEnglish() async throws {
        let korean = try await samples("ko-long.wav")
        let english = try await samples("en-long.wav")
        let source = ArraySource(samples: korean + [Float](repeating: 0, count: PCMFormat.sampleRate) + english)
        let checkpoint = FileManager.default.temporaryDirectory.appendingPathComponent("sillok-native-\(UUID().uuidString)/checkpoint.json")
        let pipeline = FileTranscriptionPipeline(
            core: NativeCore(engine: Self.engine, vadModel: vad),
            options: TranscriptionOptions(languageMode: .koreanEnglish, beamSize: 1),
            modelID: model.lastPathComponent,
            checkpointURL: checkpoint
        )
        var segments: [Segment] = []
        for try await progress in pipeline.run(source: source) {
            segments += progress.newSegments
        }
        let koreanEnd = Double(korean.count) / Double(PCMFormat.sampleRate)
        let first = segments.filter { $0.end <= koreanEnd + 0.5 }
        let second = segments.filter { $0.start >= koreanEnd + 0.5 }
        #expect(!first.isEmpty && first.allSatisfy { $0.language == "ko" })
        #expect(!second.isEmpty && second.allSatisfy { $0.language == "en" })
        #expect(second.map(\.text).joined().lowercased().contains("format"))
        #expect(segments.allSatisfy { $0.end - $0.start <= 7 + 1e-6 })
        let saved = try VersionedStore.load(Checkpoint.self, from: checkpoint)
        #expect(saved.state.isComplete(chunkCount: saved.chunks.count))
    }
}
