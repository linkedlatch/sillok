import AVFoundation
import SillokCore

public struct AssetAudioSource: AudioStreamSource {
    public let url: URL
    public let duration: TimeInterval
    private let asset: AVURLAsset
    private let trackID: CMPersistentTrackID

    public init(url: URL) async throws {
        guard FormatProbe.isSupported(url) else { throw AudioImportError.unsupportedFormat }
        let asset = AVURLAsset(url: url)
        let tracks: [AVAssetTrack]
        do {
            tracks = try await asset.loadTracks(withMediaType: .audio)
        } catch {
            throw AudioImportError.unsupportedFormat
        }
        guard let track = tracks.first else { throw AudioImportError.noAudioTrack }
        self.url = url
        self.asset = asset
        self.trackID = track.trackID
        self.duration = try await track.load(.timeRange).end.seconds
    }

    public func read(from start: TimeInterval, to end: TimeInterval) async throws -> [Float] {
        let rate = Double(PCMFormat.sampleRate)
        let first = Int((max(start, 0) * rate).rounded())
        let available = Int((duration * rate).rounded())
        let last = min(Int((end * rate).rounded()), available)
        guard last > first else { return [] }

        guard let loaded = try await asset.loadTrack(withTrackID: trackID) else { throw AudioImportError.noAudioTrack }
        nonisolated(unsafe) let track = loaded
        try Task.checkCancellation()
        let asset = asset
        return try await withCheckedThrowingContinuation { continuation in
            Self.decodeQueue.async {
                continuation.resume(with: Result {
                    try Self.decode(asset: asset, track: track, first: first, last: last, available: available)
                })
            }
        }
    }

    private static let decodeQueue = DispatchQueue(label: "sillok.audio.decode", qos: .userInitiated)

    private static let resamplerMargin = PCMFormat.sampleRate / 10

    private static func decode(asset: AVURLAsset, track: AVAssetTrack, first: Int, last: Int, available: Int) throws -> [Float] {
        let rate = Double(PCMFormat.sampleRate)
        let reader = try AVAssetReader(asset: asset)
        reader.timeRange = CMTimeRange(
            start: CMTime(value: CMTimeValue(max(first - resamplerMargin, 0)), timescale: CMTimeScale(PCMFormat.sampleRate)),
            end: CMTime(value: CMTimeValue(min(last + resamplerMargin, available)), timescale: CMTimeScale(PCMFormat.sampleRate))
        )
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: Self.outputSettings)
        output.alwaysCopiesSampleData = false
        reader.add(output)
        guard reader.startReading() else { throw reader.error ?? AudioImportError.unsupportedFormat }
        defer { reader.cancelReading() }

        var samples: [Float] = []
        samples.reserveCapacity(last - first)
        while let buffer = output.copyNextSampleBuffer() {
            let bufferStart = Int((CMSampleBufferGetPresentationTimeStamp(buffer).seconds * rate).rounded())
            let count = CMSampleBufferGetNumSamples(buffer)
            let lower = max(first - bufferStart, 0)
            let upper = min(last - bufferStart, count)
            guard upper > lower else { continue }
            try Self.append(buffer, range: lower..<upper, to: &samples)
        }
        if reader.status == .failed { throw reader.error ?? AudioImportError.unsupportedFormat }
        if samples.count < last - first {
            samples.append(contentsOf: repeatElement(0, count: last - first - samples.count))
        }
        return samples
    }

    private static var outputSettings: [String: Any] {
        [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: PCMFormat.sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 32,
            AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
        ]
    }

    private static func append(_ buffer: CMSampleBuffer, range: Range<Int>, to samples: inout [Float]) throws {
        guard let block = CMSampleBufferGetDataBuffer(buffer) else { return }
        let stride = MemoryLayout<Float>.size
        let base = samples.count
        samples.append(contentsOf: repeatElement(0, count: range.count))
        let status = samples.withUnsafeMutableBytes { raw in
            CMBlockBufferCopyDataBytes(
                block,
                atOffset: range.lowerBound * stride,
                dataLength: range.count * stride,
                destination: raw.baseAddress! + base * stride
            )
        }
        guard status == kCMBlockBufferNoErr else {
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(status))
        }
    }
}
