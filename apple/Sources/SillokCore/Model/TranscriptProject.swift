import Foundation

public enum SourceReference: Codable, Hashable, Sendable {
    case bookmark(Data)
    case containerCopy(fileName: String)
}

public enum TranscriptStatus: Codable, Hashable, Sendable {
    case pending
    case transcribing
    case completed
    case cancelled
    case failed(String)
}

public struct TranscriptProject: VersionedDocument, Hashable, Identifiable, Sendable {
    public static let currentSchemaVersion = 1

    public var schemaVersion: Int
    public var id: UUID
    public var createdAt: Date
    public var updatedAt: Date
    public var sourceName: String
    public var source: SourceReference
    public var duration: TimeInterval?
    public var modelID: String?
    public var options: TranscriptionOptions
    public var status: TranscriptStatus
    public var segments: [Segment]

    public init(
        id: UUID = UUID(),
        sourceName: String,
        source: SourceReference,
        options: TranscriptionOptions,
        createdAt: Date = Date()
    ) {
        self.schemaVersion = Self.currentSchemaVersion
        self.id = id
        self.createdAt = createdAt
        self.updatedAt = createdAt
        self.sourceName = sourceName
        self.source = source
        self.duration = nil
        self.modelID = nil
        self.options = options
        self.status = .pending
        self.segments = []
    }
}
