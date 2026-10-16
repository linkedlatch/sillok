import Foundation

public struct LanguageResolver: Sendable {
    public var mode: LanguageMode
    public var shortChunk: TimeInterval = 3
    public var fallbackLanguage = "ko"
    public var switchConfidence = 0.8

    public init(mode: LanguageMode) {
        self.mode = mode
    }

    public func resolve(
        chunkIndex: Int,
        samples: [Float],
        chunks: [AudioChunk],
        previousLanguage: String?,
        source: AudioStreamSource,
        engine: TranscriptionEngine
    ) async throws -> String {
        if case .fixed(let code) = mode { return code }

        let chunk = chunks[chunkIndex]
        guard chunk.duration < shortChunk else {
            let detection = try await engine.detectLanguage(samples: samples, allowedLanguages: mode.allowedLanguages)
            guard let previousLanguage, detection.language != previousLanguage else { return detection.language }
            guard detection.confidence >= switchConfidence else { return previousLanguage }
            guard chunkIndex + 1 < chunks.count else { return detection.language }
            let next = chunks[chunkIndex + 1]
            let confirmation = try await engine.detectLanguage(
                samples: try await source.read(from: next.start, to: next.end),
                allowedLanguages: mode.allowedLanguages
            )
            let confirmed = confirmation.language == detection.language && confirmation.confidence >= switchConfidence
            return confirmed ? detection.language : previousLanguage
        }
        if let previousLanguage { return previousLanguage }

        var combined = samples
        var index = chunkIndex + 1
        let needed = Int(shortChunk * Double(PCMFormat.sampleRate))
        while combined.count < needed, index < chunks.count {
            combined += try await source.read(from: chunks[index].start, to: chunks[index].end)
            index += 1
        }
        guard combined.count >= needed else { return fallbackLanguage }
        return try await engine.detectLanguage(samples: combined, allowedLanguages: mode.allowedLanguages).language
    }
}
