import Foundation

public struct WordTimestamp: Codable, Hashable, Sendable {
    public var word: String
    public var start: TimeInterval
    public var end: TimeInterval
    public var probability: Float

    public init(word: String, start: TimeInterval, end: TimeInterval, probability: Float) {
        self.word = word
        self.start = start
        self.end = end
        self.probability = probability
    }
}

public struct Segment: Codable, Hashable, Identifiable, Sendable {
    public var id: UUID
    public var start: TimeInterval
    public var end: TimeInterval
    public var text: String
    public var language: String
    public var speaker: String?
    public var words: [WordTimestamp]?
    public var avgLogprob: Float?
    public var noSpeechProb: Float?
    public var compressionRatio: Float?
    public var isLowConfidence: Bool

    public init(
        id: UUID = UUID(),
        start: TimeInterval,
        end: TimeInterval,
        text: String,
        language: String,
        speaker: String? = nil,
        words: [WordTimestamp]? = nil,
        avgLogprob: Float? = nil,
        noSpeechProb: Float? = nil,
        compressionRatio: Float? = nil,
        isLowConfidence: Bool = false
    ) {
        self.id = id
        self.start = start
        self.end = end
        self.text = text
        self.language = language
        self.speaker = speaker
        self.words = words
        self.avgLogprob = avgLogprob
        self.noSpeechProb = noSpeechProb
        self.compressionRatio = compressionRatio
        self.isLowConfidence = isLowConfidence
    }
}

public struct SpeakerTurn: Codable, Hashable, Sendable {
    public var start: TimeInterval
    public var end: TimeInterval
    public var speaker: String

    public init(start: TimeInterval, end: TimeInterval, speaker: String) {
        self.start = start
        self.end = end
        self.speaker = speaker
    }
}
