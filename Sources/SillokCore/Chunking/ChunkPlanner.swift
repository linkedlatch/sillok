import Foundation

public struct AudioChunk: Codable, Hashable, Sendable {
    public var start: TimeInterval
    public var end: TimeInterval
    public var cutBefore: TimeInterval?
    public var cutAfter: TimeInterval?

    public init(start: TimeInterval, end: TimeInterval, cutBefore: TimeInterval? = nil, cutAfter: TimeInterval? = nil) {
        self.start = start
        self.end = end
        self.cutBefore = cutBefore
        self.cutAfter = cutAfter
    }

    public var duration: TimeInterval { end - start }
}

public struct ChunkPlanner: Sendable {
    public var speechThreshold: Float = 0.5
    public var silenceThreshold: Float = 0.35
    public var minSilence: TimeInterval = 0.5
    public var speechPad: TimeInterval = 0.2
    public var maxChunk: TimeInterval = 30
    public var forcedCutSearchStart: TimeInterval = 25
    public var overlap: TimeInterval = 0.5

    public init() {}

    public func plan(_ map: SpeechMap) -> [AudioChunk] {
        var chunks: [AudioChunk] = []
        var current: AudioChunk?

        for region in speechRegions(map) {
            if var chunk = current, region.upperBound - chunk.start <= maxChunk {
                chunk.end = region.upperBound
                current = chunk
                continue
            }
            if let chunk = current { chunks.append(chunk) }

            var chunk = AudioChunk(start: region.lowerBound, end: region.upperBound)
            while chunk.end - chunk.start > maxChunk {
                let cut = quietestTime(map, from: chunk.start + forcedCutSearchStart, to: chunk.start + maxChunk - overlap)
                chunks.append(AudioChunk(start: chunk.start, end: cut + overlap, cutBefore: chunk.cutBefore, cutAfter: cut))
                chunk = AudioChunk(start: cut - overlap, end: chunk.end, cutBefore: cut)
            }
            current = chunk
        }
        if let chunk = current { chunks.append(chunk) }
        return chunks
    }

    func speechRegions(_ map: SpeechMap) -> [ClosedRange<TimeInterval>] {
        var raw: [ClosedRange<Int>] = []
        var speechStart: Int?
        for (index, probability) in map.probabilities.enumerated() {
            if let start = speechStart {
                if probability < silenceThreshold {
                    raw.append(start...(index - 1))
                    speechStart = nil
                }
            } else if probability >= speechThreshold {
                speechStart = index
            }
        }
        if let start = speechStart { raw.append(start...(map.probabilities.count - 1)) }

        let minSilenceFrames = Int((minSilence / map.frameDuration).rounded(.up))
        var merged: [ClosedRange<Int>] = []
        for range in raw {
            if let last = merged.last, range.lowerBound - last.upperBound - 1 < minSilenceFrames {
                merged[merged.count - 1] = last.lowerBound...range.upperBound
            } else {
                merged.append(range)
            }
        }

        return merged.map { range in
            let start = Double(range.lowerBound) * map.frameDuration - speechPad
            let end = Double(range.upperBound + 1) * map.frameDuration + speechPad
            return max(start, 0)...min(end, map.duration)
        }
    }

    func quietestTime(_ map: SpeechMap, from start: TimeInterval, to end: TimeInterval) -> TimeInterval {
        let first = max(Int((start / map.frameDuration).rounded(.up)), 0)
        let last = min(Int((end / map.frameDuration).rounded(.down)) - 1, map.energies.count - 1)
        guard first <= last else { return end }
        let index = (first...last).min { map.energies[$0] < map.energies[$1] }!
        return min(max((Double(index) + 0.5) * map.frameDuration, start), end)
    }
}
