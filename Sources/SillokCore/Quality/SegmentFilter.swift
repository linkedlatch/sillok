import Foundation

public struct SegmentFilter: Sendable {
    public var noSpeechThreshold: Float = 0.6
    public var logprobThreshold: Float = -1.0
    public var compressionRatioThreshold: Float = 2.4

    public init() {}

    public func apply(_ segments: [Segment]) -> [Segment] {
        segments.compactMap { segment in
            if isSilenceHallucination(segment) { return nil }
            var kept = segment
            kept.isLowConfidence = isLowConfidence(segment)
            return kept
        }
    }

    func isSilenceHallucination(_ segment: Segment) -> Bool {
        guard let noSpeech = segment.noSpeechProb, let logprob = segment.avgLogprob else { return false }
        return noSpeech > noSpeechThreshold && logprob < logprobThreshold
    }

    func isLowConfidence(_ segment: Segment) -> Bool {
        if let ratio = segment.compressionRatio, ratio > compressionRatioThreshold { return true }
        if let logprob = segment.avgLogprob, logprob < logprobThreshold { return true }
        return false
    }
}
