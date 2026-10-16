import Foundation

public enum ComputeMode: Sendable {
    case foregroundAccelerated
    case cpuOnly
}

public struct ModelFiles: Sendable, Hashable {
    public var ggml: URL
    public var coreMLEncoder: URL?

    public init(ggml: URL, coreMLEncoder: URL? = nil) {
        self.ggml = ggml
        self.coreMLEncoder = coreMLEncoder
    }

    public static func coreMLEncoderName(forGGML name: String) -> String {
        var stem = (name as NSString).deletingPathExtension
        if let dash = stem.lastIndex(of: "-") {
            let suffix = stem[dash...]
            let chars = Array(suffix)
            if chars.count == 5, chars[1] == "q", chars[3] == "_" {
                stem = String(stem[..<dash])
            }
        }
        return stem + "-encoder.mlmodelc"
    }
}

public struct ChunkTranscribeOptions: Sendable, Hashable {
    public var language: String
    public var prompt: String?
    public var beamSize: Int

    public init(language: String, prompt: String? = nil, beamSize: Int = 5) {
        self.language = language
        self.prompt = prompt
        self.beamSize = beamSize
    }
}

public struct LanguageDetection: Hashable, Sendable {
    public var language: String
    public var confidence: Double

    public init(language: String, confidence: Double) {
        self.language = language
        self.confidence = confidence
    }
}

public protocol TranscriptionEngine: Sendable {
    func loadModel(_ files: ModelFiles, mode: ComputeMode) async throws
    func unloadModel() async
    func detectLanguage(samples: [Float], allowedLanguages: Set<String>?) async throws -> LanguageDetection
    func tokenCount(_ text: String) async throws -> Int
    func transcribe(samples: [Float], options: ChunkTranscribeOptions) async throws -> [Segment]
}
