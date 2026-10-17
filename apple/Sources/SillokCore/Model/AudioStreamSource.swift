import Foundation

public enum PCMFormat {
    public static let sampleRate = 16_000
}

public protocol AudioStreamSource: Sendable {
    var duration: TimeInterval { get }
    func read(from start: TimeInterval, to end: TimeInterval) throws -> [Float]
}

extension AudioStreamSource {
    public func readAsync(from start: TimeInterval, to end: TimeInterval) async throws -> [Float] {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result { try read(from: start, to: end) })
            }
        }
    }
}
