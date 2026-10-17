import Foundation

public struct ModelManifest: Codable, Hashable, Sendable {
    public var version: Int
    public var models: [ModelEntry]

    public init(version: Int, models: [ModelEntry]) {
        self.version = version
        self.models = models
    }

    public func entry(_ id: String) -> ModelEntry? {
        models.first { $0.id == id }
    }

    public func entry(sha256: String) -> ModelEntry? {
        models.first { $0.sha256.lowercased() == sha256.lowercased() }
    }

    public func coreMLEncoder(for whisper: ModelEntry) -> ModelEntry? {
        whisper.coreMLEncoderID.flatMap { entry($0) }
    }
}

public struct ModelEntry: Codable, Hashable, Identifiable, Sendable {
    public enum Kind: String, Codable, Sendable {
        case whisper
        case coreMLEncoder
        case vad
        case diarization
    }

    public var id: String
    public var kind: Kind
    public var displayName: String
    public var fileName: String
    public var size: Int64
    public var sha256: String
    public var urls: [URL]
    public var coreMLEncoderID: String?
    public var license: String

    public init(id: String, kind: Kind, displayName: String, fileName: String, size: Int64, sha256: String, urls: [URL], coreMLEncoderID: String? = nil, license: String) {
        self.id = id
        self.kind = kind
        self.displayName = displayName
        self.fileName = fileName
        self.size = size
        self.sha256 = sha256
        self.urls = urls
        self.coreMLEncoderID = coreMLEncoderID
        self.license = license
    }

    public var installedName: String {
        kind == .coreMLEncoder && fileName.hasSuffix(".zip") ? String(fileName.dropLast(4)) : fileName
    }
}
