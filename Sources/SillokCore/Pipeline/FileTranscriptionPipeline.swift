import Foundation

public struct TranscribeProgress: Sendable {
    public enum Phase: Sendable, Equatable {
        case analyzing
        case transcribing
    }

    public var phase: Phase
    public var fractionCompleted: Double
    public var lastProcessedTimestamp: TimeInterval
    public var newSegments: [Segment]
    public var processingLag: TimeInterval?
}

public struct FileTranscriptionPipeline: Sendable {
    public var engine: TranscriptionEngine
    public var vad: VoiceActivityDetector
    public var options: TranscriptionOptions
    public var modelID: String
    public var checkpointURL: URL
    public var planner = ChunkPlanner()
    public var thermal: ThermalStateProvider?
    public var cooldown: Duration = .seconds(2)

    public init(engine: TranscriptionEngine, vad: VoiceActivityDetector, options: TranscriptionOptions, modelID: String, checkpointURL: URL) {
        self.engine = engine
        self.vad = vad
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
            let map = try await SpeechMap.build(source: source, vad: vad)
            checkpoint = Checkpoint(modelID: modelID, options: options, chunks: planner.plan(map))
            try VersionedStore.save(checkpoint, to: checkpointURL)
        }

        let transcriber = ChunkTranscriber(engine: engine, options: options)
        while !checkpoint.state.isComplete(chunkCount: checkpoint.chunks.count) {
            try Task.checkCancellation()
            if checkpoint.state.nextChunk > 0, let thermal, thermal.level >= .serious {
                try await Task.sleep(for: cooldown)
            }
            let before = checkpoint.state.segments.count
            checkpoint.state = try await transcriber.step(checkpoint.state, chunks: checkpoint.chunks, source: source)
            try VersionedStore.save(checkpoint, to: checkpointURL)
            emit(TranscribeProgress(
                phase: .transcribing,
                fractionCompleted: fraction(checkpoint.resumeFrom, source.duration),
                lastProcessedTimestamp: checkpoint.resumeFrom,
                newSegments: Array(checkpoint.state.segments[before...])
            ))
        }
        emit(TranscribeProgress(phase: .transcribing, fractionCompleted: 1, lastProcessedTimestamp: source.duration, newSegments: []))
    }

    private func fraction(_ time: TimeInterval, _ duration: TimeInterval) -> Double {
        duration > 0 ? min(time / duration, 1) : 1
    }
}
