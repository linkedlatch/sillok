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

@Test func timedSegmentsReplaceTheDraftOfTheirChunk() throws {
    func preview(_ chunk: Int, draft: Bool, _ texts: String...) throws -> Preview {
        let items = texts.map {
            #"{"start_ms": 0, "end_ms": 900, "text": "\#($0)", "language": "ko", "words": [], "chunk": \#(chunk)"#
                + (draft ? #", "draft": true}"# : "}")
        }
        return try #require(try Preview(json: "[\(items.joined(separator: ","))]"))
    }
    var previews = Previews()
    previews.add(try preview(0, draft: true, "하나 둘"))
    previews.add(try preview(1, draft: true, "셋"))
    #expect(previews.segments.map(\.text) == ["하나 둘", "셋"])
    previews.add(try preview(0, draft: false, "하나", "둘"))
    #expect(previews.segments.map(\.text) == ["하나", "둘", "셋"])
    previews.add(try preview(1, draft: false, "셋."))
    #expect(previews.segments.map(\.text) == ["하나", "둘", "셋."])
    #expect(previews.pieces.allSatisfy { !$0.draft })
}

struct RealModels {
    static let environment = ProcessInfo.processInfo.environment
    static let available = environment["SILLOK_TEST_MODELS"] != nil && environment["SILLOK_TEST_AUDIO"] != nil

    let engine: Engine
    let audio: URL
    let copy = FileManager.default.temporaryDirectory.appending(path: "sillok-\(UUID().uuidString).m4a")
    let checkpoint = FileManager.default.temporaryDirectory.appending(path: "sillok-\(UUID().uuidString).jsonl")
    let speech = FileManager.default.temporaryDirectory.appending(path: "sillok-\(UUID().uuidString).bin")

    init() throws {
        let models = URL(filePath: Self.environment["SILLOK_TEST_MODELS"]!)
        engine = try Engine(config: EngineConfig(modelsDirectory: models))
        audio = URL(filePath: Self.environment["SILLOK_TEST_AUDIO"]!)
    }
}

@Test(.enabled(if: RealModels.available))
func transcribesACopyOfARecording() async throws {
    let real = try RealModels()
    defer {
        try? FileManager.default.removeItem(at: real.copy)
        try? FileManager.default.removeItem(at: real.speech)
    }
    try await Transcription.copy(engine: real.engine, input: real.audio, output: real.copy) { _ in }
    #expect(FileManager.default.fileExists(atPath: real.copy.path()))

    let events = Mutex<[TranscriptionEvent]>([])
    let result = try await Transcription.run(
        engine: real.engine, copy: real.copy, options: JobOptions(diarize: true), checkpoint: real.checkpoint,
        speech: real.speech
    ) { event in events.withLock { $0.append(event) } }

    #expect(!result.transcript.segments.isEmpty)
    #expect(result.transcript.segments.allSatisfy { $0.speaker != nil })
    #expect(!FileManager.default.fileExists(atPath: real.checkpoint.path()))
    #expect(FileManager.default.fileExists(atPath: real.speech.path()))
    let stages = events.withLock { $0.compactMap { if case .stage(let stage, _) = $0 { stage } else { nil } } }
    #expect(Set(stages).isSuperset(of: [.analyze, .transcribe, .diarize]))
    let previews = events.withLock { $0.contains { if case .preview = $0 { true } else { false } } }
    #expect(previews)
    print("TRANSCRIPT", result.transcript.segments.map { "[\($0.speaker ?? -1)] \($0.text)" })
}

@Test(.enabled(if: RealModels.available))
func cancellingStopsTheCopy() async throws {
    let real = try RealModels()
    let task = Task {
        try await Transcription.copy(engine: real.engine, input: real.audio, output: real.copy) { _ in }
    }
    task.cancel()
    await #expect(throws: CancellationError.self) { try await task.value }
    #expect(!FileManager.default.fileExists(atPath: real.copy.path()))
}
