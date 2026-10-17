import Foundation

public protocol VersionedDocument: Codable {
    static var currentSchemaVersion: Int { get }
    var schemaVersion: Int { get set }
    static func migrate(_ json: inout [String: Any], from version: Int) throws
}

extension VersionedDocument {
    public static func migrate(_ json: inout [String: Any], from version: Int) throws {}
}

public enum VersionedStoreError: Error, Equatable {
    case unsupportedVersion(Int)
}

public enum VersionedStore {
    private struct Probe: Decodable {
        var schemaVersion: Int
    }

    public static func load<T: VersionedDocument>(_ type: T.Type, from url: URL) throws -> T {
        var data = try Data(contentsOf: url)
        let version = try decoder.decode(Probe.self, from: data).schemaVersion
        guard version <= T.currentSchemaVersion else { throw VersionedStoreError.unsupportedVersion(version) }
        if version < T.currentSchemaVersion {
            guard var json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                throw VersionedStoreError.unsupportedVersion(version)
            }
            for from in version..<T.currentSchemaVersion {
                try T.migrate(&json, from: from)
            }
            json["schemaVersion"] = T.currentSchemaVersion
            data = try JSONSerialization.data(withJSONObject: json)
        }
        return try decoder.decode(T.self, from: data)
    }

    public static func save<T: VersionedDocument>(_ document: T, to url: URL) throws {
        var document = document
        document.schemaVersion = T.currentSchemaVersion
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try encoder.encode(document).write(to: url, options: .atomic)
    }

    private static var encoder: JSONEncoder {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        return encoder
    }

    private static var decoder: JSONDecoder {
        JSONDecoder()
    }
}
