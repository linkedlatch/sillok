import Foundation

public enum PCMFormat {
    public static let sampleRate = 16_000
}

public protocol AudioStreamSource: Sendable {
    var duration: TimeInterval { get }
    func read(from start: TimeInterval, to end: TimeInterval) async throws -> [Float]
}
