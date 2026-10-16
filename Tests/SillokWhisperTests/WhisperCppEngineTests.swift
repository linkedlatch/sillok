import Foundation
import Testing
import SillokCore
@testable import SillokWhisper

private let pangram = "The quick brown fox jumps over the lazy dog."

@Suite(.serialized, .enabled(if: TestSupport.hasTinyModel))
struct WhisperCppEngineTests {
    @Test(arguments: [
        (ComputeMode.foregroundAccelerated, false),
        (ComputeMode.foregroundAccelerated, true),
        (ComputeMode.cpuOnly, false),
    ])
    func transcribesEnglishSpeech(mode: ComputeMode, coreML: Bool) async throws {
        let samples = try await TestSupport.speech(pangram, voice: "Samantha")
        let engine = WhisperCppEngine()
        try await engine.loadModel(
            ModelFiles(ggml: TestSupport.tinyModel, coreMLEncoder: coreML ? TestSupport.tinyEncoder : nil),
            mode: mode
        )
        let segments = try await engine.transcribe(samples: samples, options: ChunkTranscribeOptions(language: "en"))
        let text = segments.map(\.text).joined(separator: " ").lowercased()

        #expect(text.contains("fox"))
        #expect(text.contains("dog"))
        let duration = Double(samples.count) / Double(PCMFormat.sampleRate)
        for segment in segments {
            #expect(segment.start >= 0)
            #expect(segment.end <= duration + 0.5)
            #expect(segment.language == "en")
            #expect(segment.avgLogprob != nil)
            #expect(segment.noSpeechProb != nil)
        }
    }

    @Test func transcribeWithoutModelThrows() async {
        await #expect(throws: WhisperEngineError.modelNotLoaded) {
            try await WhisperCppEngine().transcribe(samples: [0], options: ChunkTranscribeOptions(language: "en"))
        }
    }

    @Test func cancellationStopsInferenceWithinOneSecond() async throws {
        let sentence = try await TestSupport.speech(String(repeating: pangram + " ", count: 6), voice: "Samantha")
        let samples = Array(repeatElement(sentence, count: 6).joined())
        let engine = WhisperCppEngine()
        try await engine.loadModel(ModelFiles(ggml: TestSupport.tinyModel), mode: .cpuOnly)

        let task = Task {
            try await engine.transcribe(samples: samples, options: ChunkTranscribeOptions(language: "en"))
        }
        try await Task.sleep(for: .milliseconds(300))
        let clock = ContinuousClock()
        let cancelledAt = clock.now
        task.cancel()
        let result = await task.result
        let elapsed = clock.now - cancelledAt

        #expect(throws: CancellationError.self) { try result.get() }
        #expect(elapsed < .seconds(1))
    }

    @Test func repeatedLoadUnloadDoesNotLeak() async throws {
        let engine = WhisperCppEngine()
        let files = ModelFiles(ggml: TestSupport.tinyModel)
        try await engine.loadModel(files, mode: .foregroundAccelerated)
        await engine.unloadModel()
        let baseline = TestSupport.footprintMB()
        for _ in 0..<5 {
            try await engine.loadModel(files, mode: .foregroundAccelerated)
            await engine.unloadModel()
        }
        #expect(TestSupport.footprintMB() - baseline < 30)
    }
}
