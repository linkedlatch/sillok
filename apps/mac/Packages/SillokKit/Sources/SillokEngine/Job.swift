import CSillok
import Foundation

public struct JobOptions: Codable, Sendable, Hashable {
    public var language: String
    public var diarize: Bool
    public var glossary: [String]

    public init(language: String = "auto", diarize: Bool = false, glossary: [String] = []) {
        self.language = language
        self.diarize = diarize
        self.glossary = glossary
    }
}

public enum Stage: Int, Sendable, CaseIterable {
    case analyze = 0
    case transcribe = 1
    case align = 2
    case diarize = 3
}

public enum TranscriptionEvent: Sendable, Equatable {
    case reading(Double)
    case stage(Stage, Double)
    case segments([Transcript.Segment])
    case saving(Double)
}

public struct TranscriptionResult: Sendable {
    public let json: String
    public let transcript: Transcript
}

public enum Transcription {
    public static func run(
        engine: Engine,
        input: URL,
        options: JobOptions,
        audioOutput: URL,
        events: @escaping @Sendable (TranscriptionEvent) -> Void
    ) async throws -> TranscriptionResult {
        let job = try Job(engine: engine, options: options)
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                let thread = Thread {
                    continuation.resume(with: Result {
                        try job.transcribe(input: input, audioOutput: audioOutput, events: events)
                    })
                }
                thread.stackSize = 8 << 20
                thread.qualityOfService = .userInitiated
                thread.start()
            }
        } onCancel: {
            job.cancel()
        }
    }
}

final class Job: @unchecked Sendable {
    private let handle: OpaquePointer

    init(engine: Engine, options: JobOptions) throws {
        var job: OpaquePointer?
        try check(sillok_job_create(engine.handle, try encodeJSON(options), &job))
        guard let job else { throw SillokError(code: .internalError, message: "no job") }
        handle = job
    }

    deinit {
        sillok_job_destroy(handle)
    }

    func cancel() {
        sillok_job_cancel(handle)
    }

    func transcribe(
        input: URL,
        audioOutput: URL,
        events: @escaping @Sendable (TranscriptionEvent) -> Void
    ) throws -> TranscriptionResult {
        let callbacks = Callbacks(events: events)
        let context = Unmanaged.passUnretained(callbacks).toOpaque()
        defer { withExtendedLifetime(callbacks) {} }

        try mapCancellation {
            try check(sillok_job_push_file(handle, input.path(percentEncoded: false), { context, fraction in
                Callbacks.from(context).emit(.reading(fraction))
            }, context))
        }
        try check(sillok_job_on_segments(handle, { context, json in
            guard let json, let segments = try? Transcript.segments(json: String(cString: json)) else { return }
            Callbacks.from(context).emit(.segments(segments))
        }, context))

        let json = try mapCancellation {
            try takeString { out in
                sillok_job_run(handle, { context, stage, fraction in
                    guard let stage = Stage(rawValue: Int(stage.rawValue)) else { return }
                    Callbacks.from(context).emit(.stage(stage, fraction))
                }, context, out)
            }
        }
        let transcript = try Transcript(json: json)

        try mapCancellation {
            try check(sillok_job_save_audio(handle, audioOutput.path(percentEncoded: false), { context, fraction in
                Callbacks.from(context).emit(.saving(fraction))
            }, context))
        }
        return TranscriptionResult(json: json, transcript: transcript)
    }

    private func mapCancellation<T>(_ body: () throws -> T) throws -> T {
        do {
            return try body()
        } catch let error as SillokError where error.code == .cancelled {
            throw CancellationError()
        }
    }
}

private final class Callbacks {
    let events: @Sendable (TranscriptionEvent) -> Void
    private var lastStage: Stage?
    private var lastFraction = -1.0

    init(events: @escaping @Sendable (TranscriptionEvent) -> Void) {
        self.events = events
    }

    static func from(_ context: UnsafeMutableRawPointer?) -> Callbacks {
        Unmanaged<Callbacks>.fromOpaque(context!).takeUnretainedValue()
    }

    func emit(_ event: TranscriptionEvent) {
        switch event {
        case .stage(let stage, let fraction):
            guard stage != lastStage || fraction - lastFraction >= 0.01 || fraction >= 1 else { return }
            lastStage = stage
            lastFraction = fraction
        case .reading(let fraction), .saving(let fraction):
            guard fraction - lastFraction >= 0.01 || fraction >= 1 || fraction < lastFraction else { return }
            lastStage = nil
            lastFraction = fraction
        case .segments:
            break
        }
        events(event)
    }
}
