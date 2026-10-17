import Foundation

public struct TranscriptionOptions: Codable, Hashable, Sendable {
    public var languageMode: LanguageMode
    public var vocabulary: [String]
    public var beamSize: Int
    public var maxPromptTokens: Int
    public var speakerDiarization: Bool

    public init(languageMode: LanguageMode = .auto, vocabulary: [String] = [], beamSize: Int = 5, maxPromptTokens: Int = 150, speakerDiarization: Bool = false) {
        self.languageMode = languageMode
        self.vocabulary = vocabulary
        self.beamSize = beamSize
        self.maxPromptTokens = maxPromptTokens
        self.speakerDiarization = speakerDiarization
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        languageMode = try container.decode(LanguageMode.self, forKey: .languageMode)
        vocabulary = try container.decode([String].self, forKey: .vocabulary)
        beamSize = try container.decode(Int.self, forKey: .beamSize)
        maxPromptTokens = try container.decode(Int.self, forKey: .maxPromptTokens)
        speakerDiarization = try container.decodeIfPresent(Bool.self, forKey: .speakerDiarization) ?? false
    }
}

public struct TranscriptionState: Codable, Hashable, Sendable {
    public var nextChunk: Int
    public var previousLanguage: String?
    public var previousText: String?
    public var segments: [Segment]
    public var diarized: Bool

    public init(nextChunk: Int = 0, previousLanguage: String? = nil, previousText: String? = nil, segments: [Segment] = [], diarized: Bool = false) {
        self.nextChunk = nextChunk
        self.previousLanguage = previousLanguage
        self.previousText = previousText
        self.segments = segments
        self.diarized = diarized
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        nextChunk = try container.decode(Int.self, forKey: .nextChunk)
        previousLanguage = try container.decodeIfPresent(String.self, forKey: .previousLanguage)
        previousText = try container.decodeIfPresent(String.self, forKey: .previousText)
        segments = try container.decode([Segment].self, forKey: .segments)
        diarized = try container.decodeIfPresent(Bool.self, forKey: .diarized) ?? false
    }

    public func isComplete(chunkCount: Int) -> Bool {
        nextChunk >= chunkCount
    }
}

public protocol TranscriptionCore: Sendable {
    func plan(source: AudioStreamSource) async throws -> [AudioChunk]
    func step(_ state: TranscriptionState, chunks: [AudioChunk], source: AudioStreamSource, options: TranscriptionOptions) async throws -> TranscriptionState
    func diarize(_ segments: [Segment], source: AudioStreamSource) async throws -> [Segment]
}
