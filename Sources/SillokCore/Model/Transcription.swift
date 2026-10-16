import Foundation

public struct TranscriptionOptions: Codable, Hashable, Sendable {
    public var languageMode: LanguageMode
    public var vocabulary: [String]
    public var beamSize: Int
    public var maxPromptTokens: Int

    public init(languageMode: LanguageMode = .auto, vocabulary: [String] = [], beamSize: Int = 5, maxPromptTokens: Int = 150) {
        self.languageMode = languageMode
        self.vocabulary = vocabulary
        self.beamSize = beamSize
        self.maxPromptTokens = maxPromptTokens
    }
}

public struct TranscriptionState: Codable, Hashable, Sendable {
    public var nextChunk: Int
    public var previousLanguage: String?
    public var previousText: String?
    public var segments: [Segment]

    public init(nextChunk: Int = 0, previousLanguage: String? = nil, previousText: String? = nil, segments: [Segment] = []) {
        self.nextChunk = nextChunk
        self.previousLanguage = previousLanguage
        self.previousText = previousText
        self.segments = segments
    }

    public func isComplete(chunkCount: Int) -> Bool {
        nextChunk >= chunkCount
    }
}

public protocol TranscriptionCore: Sendable {
    func plan(source: AudioStreamSource) async throws -> [AudioChunk]
    func step(_ state: TranscriptionState, chunks: [AudioChunk], source: AudioStreamSource, options: TranscriptionOptions) async throws -> TranscriptionState
}
