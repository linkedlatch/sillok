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

}

public struct Preview: Sendable, Equatable {
    public let chunk: Int
    public let draft: Bool
    public let segments: [Transcript.Segment]

    init?(json: String) throws {
        let items = try Transcript.decoder.decode([Item].self, from: Data(json.utf8))
        guard let first = items.first else { return nil }
        chunk = first.chunk
        draft = first.draft
        segments = items.map(\.segment)
    }

    private struct Item: Decodable {
        enum CodingKeys: CodingKey { case chunk, draft }

        let segment: Transcript.Segment
        let chunk: Int
        let draft: Bool

        init(from decoder: Decoder) throws {
            segment = try Transcript.Segment(from: decoder)
            let container = try decoder.container(keyedBy: CodingKeys.self)
            chunk = try container.decode(Int.self, forKey: .chunk)
            draft = try container.decodeIfPresent(Bool.self, forKey: .draft) ?? false
        }
    }
}

public struct Previews: Sendable, Equatable {
    public private(set) var pieces: [Preview] = []

    public init() {}

    public var segments: [Transcript.Segment] { pieces.flatMap(\.segments) }

    public mutating func add(_ preview: Preview) {
        if !preview.draft {
            pieces.removeAll { $0.chunk == preview.chunk && $0.draft }
        }
        let index = pieces.firstIndex { $0.chunk > preview.chunk } ?? pieces.endIndex
        pieces.insert(preview, at: index)
    }
}
