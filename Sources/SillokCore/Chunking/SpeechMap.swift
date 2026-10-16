import Foundation

public struct SpeechMap: Codable, Hashable, Sendable {
    public var frameDuration: TimeInterval
    public var probabilities: [Float]
    public var energies: [Float]
    public var duration: TimeInterval

    public init(frameDuration: TimeInterval, probabilities: [Float], energies: [Float], duration: TimeInterval) {
        self.frameDuration = frameDuration
        self.probabilities = probabilities
        self.energies = energies
        self.duration = duration
    }

    public static func build(
        source: AudioStreamSource,
        vad: VoiceActivityDetector,
        framesPerWindow: Int = 1875
    ) async throws -> SpeechMap {
        let frameSize = vad.frameSize
        let rate = Double(PCMFormat.sampleRate)
        let windowDuration = Double(frameSize * framesPerWindow) / rate
        await vad.reset()

        var probabilities: [Float] = []
        var energies: [Float] = []
        var start: TimeInterval = 0
        while start < source.duration {
            let samples = try await source.read(from: start, to: start + windowDuration)
            guard !samples.isEmpty else { break }
            probabilities += try await vad.speechProbabilities(samples)
            energies += stride(from: 0, to: samples.count, by: frameSize).map { offset in
                let frame = samples[offset..<min(offset + frameSize, samples.count)]
                return (frame.reduce(0) { $0 + $1 * $1 } / Float(frame.count)).squareRoot()
            }
            start += windowDuration
        }
        return SpeechMap(
            frameDuration: Double(frameSize) / rate,
            probabilities: probabilities,
            energies: energies,
            duration: source.duration
        )
    }
}
