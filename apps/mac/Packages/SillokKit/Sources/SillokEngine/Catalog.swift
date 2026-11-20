import CSillok
import Foundation

public enum ModelRole: String, Codable, Sendable, CaseIterable {
    case voiceDetection = "voice_detection"
    case transcription
    case diarization
}

public struct CatalogFile: Codable, Sendable, Hashable {
    public let file: String
    public let url: URL
    public let size: Int64
    public let sha256: String
    public let zip: Bool
}

public struct CatalogModel: Codable, Sendable, Hashable, Identifiable {
    public let id: String
    public let role: ModelRole
    public let backend: String
    public let family: String
    public let quantization: String
    public let recommended: Bool
    public let files: [CatalogFile]
    public let languages: [String]?
    public let aligner: Bool?
    public let memory: Int64?
}

public struct ModelFileStatus: Codable, Sendable, Hashable {
    public enum Status: String, Codable, Sendable {
        case ok
        case missing
        case wrongSize = "wrong_size"
        case wrongHash = "wrong_hash"
        case extract
    }

    public let model: String
    public let role: ModelRole
    public let file: String
    public let status: Status
}

public enum Catalog {
    public static func models() throws -> [CatalogModel] {
        try takeJSON([CatalogModel].self) { sillok_model_catalog($0) }
    }

    public static func engineModels(transcriptionModel: String? = nil) throws -> [CatalogModel] {
        try takeJSON([CatalogModel].self) { sillok_engine_models(transcriptionModel, $0) }
    }

    public static func check(directory: URL, verifyHash: Bool = false) throws -> [ModelFileStatus] {
        try takeJSON([ModelFileStatus].self) {
            sillok_model_check(directory.path(percentEncoded: false), verifyHash ? 1 : 0, $0)
        }
    }
}
