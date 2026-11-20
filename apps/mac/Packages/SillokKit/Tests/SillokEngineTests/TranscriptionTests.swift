import Foundation
import Synchronization
import Testing

@testable import SillokEngine

@Test func engineConfigUsesTheLibraryKeys() throws {
    let config = EngineConfig(modelsDirectory: URL(filePath: "/models"))
    let object = try JSONSerialization.jsonObject(with: Data(encodeJSON(config).utf8)) as? [String: Any]
    #expect(Set(object?.keys ?? [:].keys) == ["models_dir", "device", "npu", "threads"])
    #expect(object?["models_dir"] as? String == "/models")
}

@Test func transcriptDecodes() throws {
    let json = """
        {"version": 1, "duration_ms": 61000,
         "speakers": [{"id": 0, "name": null}],
         "segments": [{"start_ms": 0, "end_ms": 2300, "text": "안녕하세요", "language": "ko", "speaker": 0,
                       "words": [{"text": "안녕하세요", "start_ms": 0, "end_ms": 420, "speaker": 0}]}],
         "models": {"transcription": {"name": "whisper", "device": "MTL0", "fell_back_to_cpu": false}}}
        """
    let transcript = try Transcript(json: json)
    #expect(transcript.durationMs == 61000)
    #expect(transcript.segments.first?.words.first?.endMs == 420)
    #expect(transcript.models["transcription"]?.fellBackToCpu == false)
}

struct RealModels {
    static let environment = ProcessInfo.processInfo.environment
    static let available = environment["SILLOK_TEST_MODELS"] != nil && environment["SILLOK_TEST_AUDIO"] != nil

    let engine: Engine
    let audio: URL
    let output = FileManager.default.temporaryDirectory.appending(path: "sillok-\(UUID().uuidString).m4a")

    init() throws {
        let models = URL(filePath: Self.environment["SILLOK_TEST_MODELS"]!)
        engine = try Engine(config: EngineConfig(modelsDirectory: models))
        audio = URL(filePath: Self.environment["SILLOK_TEST_AUDIO"]!)
    }
}

@Test(.enabled(if: RealModels.available))
func transcribesARecording() async throws {
    let real = try RealModels()
    defer { try? FileManager.default.removeItem(at: real.output) }
    let events = Mutex<[TranscriptionEvent]>([])
    let result = try await Transcription.run(
        engine: real.engine, input: real.audio, options: JobOptions(diarize: true), audioOutput: real.output
    ) { event in events.withLock { $0.append(event) } }

    #expect(!result.transcript.segments.isEmpty)
    #expect(result.transcript.segments.allSatisfy { $0.speaker != nil })
    #expect(FileManager.default.fileExists(atPath: real.output.path()))
    let stages = events.withLock { $0.compactMap { if case .stage(let stage, _) = $0 { stage } else { nil } } }
    #expect(Set(stages) == [.analyze, .transcribe, .diarize])
    let previews = events.withLock { $0.contains { if case .segments = $0 { true } else { false } } }
    #expect(previews)
    print("TRANSCRIPT", result.transcript.segments.map { "[\($0.speaker ?? -1)] \($0.text)" })
}

@Test(.enabled(if: RealModels.available))
func cancellingStopsTheJob() async throws {
    let real = try RealModels()
    let task = Task {
        try await Transcription.run(engine: real.engine, input: real.audio, options: JobOptions(),
                                    audioOutput: real.output) { _ in }
    }
    task.cancel()
    await #expect(throws: CancellationError.self) { try await task.value }
    #expect(!FileManager.default.fileExists(atPath: real.output.path()))
}
