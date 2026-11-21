import CSillok
import Foundation

public struct SillokError: Error, LocalizedError, Equatable {
    public enum Code: Equatable, Sendable {
        case invalidArgument
        case modelError
        case cancelled
        case busy
        case internalError
        case mediaError
        case unknown(Int)

        init(_ status: sillok_status) {
            switch status {
            case SILLOK_INVALID_ARGUMENT: self = .invalidArgument
            case SILLOK_MODEL_ERROR: self = .modelError
            case SILLOK_CANCELLED: self = .cancelled
            case SILLOK_BUSY: self = .busy
            case SILLOK_INTERNAL_ERROR: self = .internalError
            case SILLOK_MEDIA_ERROR: self = .mediaError
            default: self = .unknown(Int(status.rawValue))
            }
        }
    }

    public let code: Code
    public let message: String

    public var errorDescription: String? { message }
}

func check(_ status: sillok_status) throws {
    guard status != SILLOK_OK else { return }
    throw SillokError(code: .init(status), message: String(cString: sillok_last_error()))
}

func takeString(_ call: (UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> sillok_status) throws -> String {
    var pointer: UnsafeMutablePointer<CChar>?
    try check(call(&pointer))
    guard let pointer else { return "" }
    defer { sillok_free_string(pointer) }
    return String(cString: pointer)
}

func takeJSON<T: Decodable>(
    _ type: T.Type,
    _ call: (UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> sillok_status
) throws -> T {
    let decoder = JSONDecoder()
    decoder.keyDecodingStrategy = .convertFromSnakeCase
    return try decoder.decode(type, from: Data(takeString(call).utf8))
}
