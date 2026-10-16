import Foundation

public protocol VoiceActivityDetector: Sendable {
    var frameSize: Int { get }
    func reset() async
    func speechProbabilities(_ samples: [Float]) async throws -> [Float]
}
