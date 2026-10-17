import Foundation

public struct TranscribeProgress: Sendable {
    public enum Phase: Sendable, Equatable {
        case analyzing
        case transcribing
        case diarizing
    }

    public var phase: Phase
    public var fractionCompleted: Double
    public var lastProcessedTimestamp: TimeInterval
    public var newSegments: [Segment]
    public var processingLag: TimeInterval?
    public var replacesSegments = false
}

public struct FileTranscriptionPipeline: Sendable {
    public var core: TranscriptionCore
    public var options: TranscriptionOptions
    public var modelID: String
    public var checkpointURL: URL
    public var thermal: ThermalStateProvider?
    public var cooldown: Duration = .seconds(2)

    public init(core: TranscriptionCore, options: TranscriptionOptions, modelID: String, checkpointURL: URL) {
        self.core = core
        self.options = options
        self.modelID = modelID
        self.checkpointURL = checkpointURL
    }

    public func run(source: AudioStreamSource) -> AsyncThrowingStream<TranscribeProgress, Error> {
        AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    try await process(source: source) { continuation.yield($0) }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    private func process(source: AudioStreamSource, emit: (TranscribeProgress) -> Void) async throws {
        var checkpoint: Checkpoint
        if let saved = try? VersionedStore.load(Checkpoint.self, from: checkpointURL),
           saved.modelID == modelID, saved.options == options {
            checkpoint = saved
            emit(TranscribeProgress(
                phase: .transcribing,
                fractionCompleted: fraction(saved.resumeFrom, source.duration),
                lastProcessedTimestamp: saved.resumeFrom,
                newSegments: saved.state.segments
            ))
        } else {
            emit(TranscribeProgress(phase: .analyzing, fractionCompleted: 0, lastProcessedTimestamp: 0, newSegments: []))
            checkpoint = Checkpoint(modelID: modelID, options: options, chunks: try await core.plan(source: source))
            try VersionedStore.save(checkpoint, to: checkpointURL)
        }

        while !checkpoint.state.isComplete(chunkCount: checkpoint.chunks.count) {
            try Task.checkCancellation()
            if checkpoint.state.nextChunk > 0, let thermal, thermal.level >= .serious {
                try await Task.sleep(for: cooldown)
            }
            let before = checkpoint.state.segments.count
            checkpoint.state = try await core.step(checkpoint.state, chunks: checkpoint.chunks, source: source, options: options)
            try VersionedStore.save(checkpoint, to: checkpointURL)
            emit(TranscribeProgress(
                phase: .transcribing,
                fractionCompleted: fraction(checkpoint.resumeFrom, source.duration),
                lastProcessedTimestamp: checkpoint.resumeFrom,
                newSegments: Array(checkpoint.state.segments[before...])
            ))
        }
        if options.speakerDiarization, !checkpoint.state.diarized {
            try Task.checkCancellation()
            emit(TranscribeProgress(phase: .diarizing, fractionCompleted: 1, lastProcessedTimestamp: source.duration, newSegments: []))
            checkpoint.state.segments = try await core.diarize(checkpoint.state.segments, source: source)
            checkpoint.state.diarized = true
            try VersionedStore.save(checkpoint, to: checkpointURL)
            emit(TranscribeProgress(
                phase: .diarizing,
                fractionCompleted: 1,
                lastProcessedTimestamp: source.duration,
                newSegments: checkpoint.state.segments,
                replacesSegments: true
            ))
        }
        emit(TranscribeProgress(phase: options.speakerDiarization ? .diarizing : .transcribing, fractionCompleted: 1, lastProcessedTimestamp: source.duration, newSegments: []))
    }

    private func fraction(_ time: TimeInterval, _ duration: TimeInterval) -> Double {
        duration > 0 ? min(time / duration, 1) : 1
    }
}
