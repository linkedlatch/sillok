import CSillok
import Foundation

public enum NativeError: Error, Equatable, LocalizedError {
    case failed(String)

    public var errorDescription: String? {
        switch self {
        case .failed(let message): message
        }
    }
}

enum NativeCall {
    private struct Envelope<Value: Decodable>: Decodable {
        var ok: Bool
        var value: Value?
        var error: String?
        var cancelled: Bool?
    }

    static let queue = DispatchQueue(label: "sillok.native", qos: .userInitiated)

    static func run<T: Sendable>(_ work: @escaping @Sendable () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            queue.async { continuation.resume(with: Result { try work() }) }
        }
    }

    static func decode<Value: Decodable>(_ type: Value.Type, _ result: UnsafeMutablePointer<CChar>?) throws -> Value {
        guard let result else { throw NativeError.failed("no response from the native core") }
        defer { sillok_free(result) }
        let envelope = try JSONDecoder().decode(Envelope<Value>.self, from: Data(bytes: result, count: strlen(result)))
        if envelope.ok, let value = envelope.value { return value }
        if envelope.cancelled == true { throw CancellationError() }
        throw NativeError.failed(envelope.error ?? "unknown native error")
    }

    static func check(_ result: UnsafeMutablePointer<CChar>?) throws {
        _ = try decode(Bool.self, result)
    }

    static func json<T: Encodable>(_ value: T) throws -> String {
        String(decoding: try JSONEncoder().encode(value), as: UTF8.self)
    }
}

final class CancelHandle: @unchecked Sendable {
    let pointer = sillok_cancel_new()

    deinit {
        sillok_cancel_free(pointer)
    }

    func cancel() {
        sillok_cancel_set(pointer)
    }
}

func withNativeCancellation<T: Sendable>(_ work: @escaping @Sendable (OpaquePointer?) throws -> T) async throws -> T {
    let handle = CancelHandle()
    return try await withTaskCancellationHandler {
        try await NativeCall.run { try work(handle.pointer) }
    } onCancel: {
        handle.cancel()
    }
}
