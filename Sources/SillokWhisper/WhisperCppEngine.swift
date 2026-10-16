import Foundation
import SillokCore

public actor WhisperCppEngine: TranscriptionEngine {
    private var context: WhisperContext?

    public init() {}

    public var isLoaded: Bool { context != nil }

    public func loadModel(_ files: ModelFiles, mode: ComputeMode) async throws {
        context = nil
        context = try await WhisperContext.run { try WhisperContext.load(files, mode: mode) }
    }

    public func unloadModel() async {
        context = nil
    }

    public func detectLanguage(samples: [Float], allowedLanguages: Set<String>?) async throws -> LanguageDetection {
        guard let context else { throw WhisperEngineError.modelNotLoaded }
        return try await WhisperContext.run { try context.detectLanguage(samples, allowed: allowedLanguages) }
    }

    public func tokenCount(_ text: String) async throws -> Int {
        guard let context else { throw WhisperEngineError.modelNotLoaded }
        return try await WhisperContext.run { context.tokenCount(text) }
    }

    public func transcribe(samples: [Float], options: ChunkTranscribeOptions) async throws -> [Segment] {
        guard let context else { throw WhisperEngineError.modelNotLoaded }
        let abort = AbortFlag()
        return try await withTaskCancellationHandler {
            try await WhisperContext.run { try context.transcribe(samples, options: options, abort: abort) }
        } onCancel: {
            abort.set()
        }
    }
}
