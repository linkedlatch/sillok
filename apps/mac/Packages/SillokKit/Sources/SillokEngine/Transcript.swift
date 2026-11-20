import Foundation

public struct Transcript: Codable, Sendable, Equatable {
    public struct Speaker: Codable, Sendable, Equatable, Identifiable {
        public let id: Int
        public var name: String?
    }

    public struct Word: Codable, Sendable, Equatable {
        public let text: String
        public let startMs: Int
        public let endMs: Int
        public let speaker: Int?
    }

    public struct Segment: Codable, Sendable, Equatable {
        public let startMs: Int
        public let endMs: Int
        public let text: String
        public let language: String
        public let speaker: Int?
        public let words: [Word]
    }

    public struct ModelUse: Codable, Sendable, Equatable {
        public let name: String
        public let device: String
        public let fellBackToCpu: Bool?
    }

    public let version: Int
    public let durationMs: Int
    public var speakers: [Speaker]
    public let segments: [Segment]
    public let models: [String: ModelUse]

    public init(json: String) throws {
        self = try Self.decoder.decode(Transcript.self, from: Data(json.utf8))
    }

    static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return decoder
    }()

    static func segments(json: String) throws -> [Segment] {
        try decoder.decode([Segment].self, from: Data(json.utf8))
    }
}
