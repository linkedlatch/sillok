import CSillok
import Foundation

public struct EngineConfig: Encodable, Sendable, Equatable {
    public enum Device: String, Encodable, Sendable {
        case cpu
        case gpu
    }

    public var modelsDir: String
    public var device: Device
    public var npu: Bool
    public var threads: Int
    public var transcriptionModel: String?

    public init(
        modelsDirectory: URL,
        device: Device = .gpu,
        npu: Bool = true,
        threads: Int = 0,
        transcriptionModel: String? = nil
    ) {
        modelsDir = modelsDirectory.path(percentEncoded: false)
        self.device = device
        self.npu = npu
        self.threads = threads
        self.transcriptionModel = transcriptionModel
    }
}

public final class Engine: @unchecked Sendable {
    public static var version: String {
        String(cString: sillok_version())
    }

    let handle: OpaquePointer

    public init(config: EngineConfig) throws {
        let json = try encodeJSON(config)
        var engine: OpaquePointer?
        try check(sillok_engine_create(json, &engine))
        guard let engine else { throw SillokError(code: .internalError, message: "no engine") }
        handle = engine
    }

    deinit {
        sillok_engine_destroy(handle)
    }
}

func encodeJSON<T: Encodable>(_ value: T) throws -> String {
    let encoder = JSONEncoder()
    encoder.keyEncodingStrategy = .convertToSnakeCase
    return String(decoding: try encoder.encode(value), as: UTF8.self)
}
