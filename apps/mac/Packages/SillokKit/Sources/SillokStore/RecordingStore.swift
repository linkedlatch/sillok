import Foundation
import SillokEngine

public struct Recording: Codable, Sendable, Identifiable, Hashable {
    public let id: UUID
    public var title: String
    public let createdAt: Date
    public let sourceName: String
    public let options: JobOptions
    public let transcriptionModel: String?
    public var durationMs: Int?
    public var transcribedAt: Date?
    public var timingsMs: [String: Int]?
    public var error: String?

    public init(id: UUID = UUID(), title: String, createdAt: Date = .now, sourceName: String,
                options: JobOptions, transcriptionModel: String) {
        self.id = id
        self.title = title
        self.createdAt = createdAt
        self.sourceName = sourceName
        self.options = options
        self.transcriptionModel = transcriptionModel
    }

    public var date: Date { transcribedAt ?? createdAt }
}

public struct RecordingStore: Sendable {
    public static let audioName = "audio.m4a"
    static let transcriptName = "transcript.json"
    static let metadataName = "meta.json"
    static let checkpointName = "checkpoint.jsonl"
    static let speechName = "speech.bin"
    static let editsName = "edits.json"

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

    public func checkpoint(for id: UUID) -> URL {
        folder(for: id).appending(path: Self.checkpointName)
    }

    public func speech(for id: UUID) -> URL {
        folder(for: id).appending(path: Self.speechName)
    }

    public func makeStaging() throws -> URL {
        let staging = root.appending(path: ".staging-\(UUID().uuidString)", directoryHint: .isDirectory)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
        return staging
    }

    public func add(staging: URL, recording: Recording) throws {
        try Self.encoder.encode(recording).write(to: staging.appending(path: Self.metadataName))
        try FileManager.default.moveItem(at: staging, to: folder(for: recording.id))
    }

    public func add(copyOf source: UUID, as recording: Recording) throws {
        let staging = try makeStaging()
        do {
            try FileManager.default.copyItem(at: audio(for: source), to: staging.appending(path: Self.audioName))
            try? FileManager.default.copyItem(at: speech(for: source), to: staging.appending(path: Self.speechName))
            try add(staging: staging, recording: recording)
        } catch {
            discard(staging: staging)
            throw error
        }
    }

    public func update(_ id: UUID, _ change: (inout Recording) -> Void) throws {
        let url = folder(for: id).appending(path: Self.metadataName)
        var recording = try Self.decoder.decode(Recording.self, from: Data(contentsOf: url))
        change(&recording)
        try Self.encoder.encode(recording).write(to: url, options: .atomic)
    }

    public func finish(_ id: UUID, transcriptJSON: String, durationMs: Int, timingsMs: [String: Int]) throws {
        try Data(transcriptJSON.utf8).write(to: folder(for: id).appending(path: Self.transcriptName), options: .atomic)
        try update(id) {
            $0.durationMs = durationMs
            $0.transcribedAt = .now
            $0.timingsMs = ($0.timingsMs ?? [:]).merging(timingsMs) { $1 }
            $0.error = nil
        }
    }

    public func discard(staging: URL) {
        try? FileManager.default.removeItem(at: staging)
    }

    public func list() throws -> [Recording] {
        try all(transcribed: true).sorted { ($0.date, $0.createdAt) > ($1.date, $1.createdAt) }
    }

    public func pending() throws -> [Recording] {
        try all(transcribed: false).sorted { $0.createdAt < $1.createdAt }
    }

    private func all(transcribed: Bool) throws -> [Recording] {
        guard FileManager.default.fileExists(atPath: root.path(percentEncoded: false)) else { return [] }
        let folders = try FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)
        return folders
            .filter { Self.exists($0.appending(path: Self.transcriptName)) == transcribed }
            .compactMap { try? Data(contentsOf: $0.appending(path: Self.metadataName)) }
            .compactMap { try? Self.decoder.decode(Recording.self, from: $0) }
    }

    private static func exists(_ url: URL) -> Bool {
        FileManager.default.fileExists(atPath: url.path(percentEncoded: false))
    }

    public func transcript(for id: UUID) throws -> Transcript {
        try Transcript(json: transcriptJSON(for: id))
    }

    public func editedTranscript(for id: UUID) throws -> Transcript {
        try transcript(for: id).applying(edits(for: id))
    }

    func transcriptJSON(for id: UUID) throws -> String {
        try String(contentsOf: folder(for: id).appending(path: Self.transcriptName), encoding: .utf8)
    }

    public func edits(for id: UUID) throws -> TranscriptEdits {
        let url = folder(for: id).appending(path: Self.editsName)
        guard Self.exists(url) else { return TranscriptEdits() }
        return try Self.decoder.decode(TranscriptEdits.self, from: Data(contentsOf: url))
    }

    public func save(_ edits: TranscriptEdits, for id: UUID) throws {
        let url = folder(for: id).appending(path: Self.editsName)
        if edits.isEmpty {
            if Self.exists(url) { try FileManager.default.removeItem(at: url) }
        } else {
            try Self.encoder.encode(edits).write(to: url, options: .atomic)
        }
    }

    public func delete(_ id: UUID) throws {
        try FileManager.default.removeItem(at: folder(for: id))
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
