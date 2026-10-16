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

public struct ChunkTranscriber: Sendable {
    public var engine: TranscriptionEngine
    public var options: TranscriptionOptions
    public var filter = SegmentFilter()
    public var splitter = SegmentSplitter()

    public init(engine: TranscriptionEngine, options: TranscriptionOptions) {
        self.engine = engine
        self.options = options
    }

    public func step(_ state: TranscriptionState, chunks: [AudioChunk], source: AudioStreamSource) async throws -> TranscriptionState {
        let chunk = chunks[state.nextChunk]
        let samples = try await source.read(from: chunk.start, to: chunk.end)

        let language = try await LanguageResolver(mode: options.languageMode).resolve(
            chunkIndex: state.nextChunk,
            samples: samples,
            chunks: chunks,
            previousLanguage: state.previousLanguage,
            source: source,
            engine: engine
        )
        let context = language == state.previousLanguage ? state.previousText : nil
        let engine = self.engine
        let prompt = try await PromptBuilder(vocabulary: options.vocabulary, maxTokens: options.maxPromptTokens)
            .build(previousText: context) { try await engine.tokenCount($0) }

        let raw = try await engine.transcribe(
            samples: samples,
            options: ChunkTranscribeOptions(language: language, prompt: prompt, beamSize: options.beamSize)
        )
        let segments = splitter.split(OverlapMerger.trim(filter.apply(raw).map { $0.offset(by: chunk.start) }, to: chunk))

        var next = state
        next.nextChunk += 1
        next.previousLanguage = language
        next.previousText = segments.contains(where: \.isLowConfidence) || segments.isEmpty
            ? nil
            : segments.map(\.text).joined(separator: " ")
        next.segments += segments
        return next
    }
}
