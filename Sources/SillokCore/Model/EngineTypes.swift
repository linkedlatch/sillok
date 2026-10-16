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
}

public struct ChunkTranscribeOptions: Codable, Sendable, Hashable {
    public var language: String
    public var prompt: String?
    public var beamSize: Int

    public init(language: String, prompt: String? = nil, beamSize: Int = 5) {
        self.language = language
        self.prompt = prompt
        self.beamSize = beamSize
    }
}

public struct LanguageDetection: Codable, Hashable, Sendable {
    public var language: String
    public var confidence: Double

    public init(language: String, confidence: Double) {
        self.language = language
        self.confidence = confidence
    }
}
