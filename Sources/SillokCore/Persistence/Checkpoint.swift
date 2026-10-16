import Foundation

public struct Checkpoint: VersionedDocument, Hashable, Sendable {
    public static let currentSchemaVersion = 1

    public var schemaVersion: Int
    public var modelID: String
    public var options: TranscriptionOptions
    public var chunks: [AudioChunk]
    public var state: TranscriptionState

    public init(modelID: String, options: TranscriptionOptions, chunks: [AudioChunk], state: TranscriptionState = TranscriptionState()) {
        self.schemaVersion = Self.currentSchemaVersion
        self.modelID = modelID
        self.options = options
        self.chunks = chunks
        self.state = state
    }

    public var resumeFrom: TimeInterval {
        state.nextChunk == 0 ? 0 : chunks[state.nextChunk - 1].end
    }
}
