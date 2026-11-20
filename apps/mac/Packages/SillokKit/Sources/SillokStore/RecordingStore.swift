import Foundation
import SillokEngine

public struct Recording: Codable, Sendable, Identifiable, Hashable {
    public let id: UUID
    public var title: String
    public let createdAt: Date
    public let sourceName: String
    public let durationMs: Int
    public let options: JobOptions

    public init(id: UUID = UUID(), title: String, createdAt: Date = .now, sourceName: String,
                durationMs: Int, options: JobOptions) {
        self.id = id
        self.title = title
        self.createdAt = createdAt
        self.sourceName = sourceName
        self.durationMs = durationMs
        self.options = options
    }
}

public struct RecordingStore: Sendable {
    public static let audioName = "audio.m4a"
    static let transcriptName = "transcript.json"
    static let metadataName = "meta.json"

    public let root: URL

    public init(root: URL) {
        self.root = root
    }

    public func folder(for id: UUID) -> URL {
        root.appending(path: id.uuidString, directoryHint: .isDirectory)
    }

    public func audio(for id: UUID) -> URL {
        folder(for: id).appending(path: Self.audioName)
    }

    public func makeStaging() throws -> URL {
        let staging = root.appending(path: ".staging-\(UUID().uuidString)", directoryHint: .isDirectory)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
        return staging
    }

    public func commit(staging: URL, recording: Recording, transcriptJSON: String) throws {
        try Data(transcriptJSON.utf8).write(to: staging.appending(path: Self.transcriptName))
        try Self.encoder.encode(recording).write(to: staging.appending(path: Self.metadataName))
        try FileManager.default.moveItem(at: staging, to: folder(for: recording.id))
    }

    public func discard(staging: URL) {
        try? FileManager.default.removeItem(at: staging)
    }

    public func list() throws -> [Recording] {
        guard FileManager.default.fileExists(atPath: root.path(percentEncoded: false)) else { return [] }
        let folders = try FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)
        return folders
            .compactMap { try? Data(contentsOf: $0.appending(path: Self.metadataName)) }
            .compactMap { try? Self.decoder.decode(Recording.self, from: $0) }
            .sorted { $0.createdAt > $1.createdAt }
    }

    public func transcriptJSON(for id: UUID) throws -> String {
        try String(contentsOf: folder(for: id).appending(path: Self.transcriptName), encoding: .utf8)
    }

    public func removeLeftoverStaging() {
        let items = (try? FileManager.default.contentsOfDirectory(atPath: root.path(percentEncoded: false))) ?? []
        for item in items where item.hasPrefix(".staging-") {
            try? FileManager.default.removeItem(at: root.appending(path: item))
        }
    }

    static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return encoder
    }()

    static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }()
}
