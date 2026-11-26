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
    case preview(Preview)
    case saving(Double)
}

public struct TranscriptionResult: Sendable {
    public let json: String
    public let transcript: Transcript
}

public enum Transcription {
    public static func copy(
        input: URL,
        output: URL,
        events: @escaping @Sendable (TranscriptionEvent) -> Void
    ) async throws {
        let job = try Job(engine: nil, request: Job.Request())
        try await job.perform { try job.copy(input: input, output: output, events: events) }
    }

    public static func run(
        engine: Engine,
        copy: URL,
        options: JobOptions,
        checkpoint: URL,
        events: @escaping @Sendable (TranscriptionEvent) -> Void
    ) async throws -> TranscriptionResult {
        let request = Job.Request(
            language: options.language, diarize: options.diarize, glossary: options.glossary,
            evenLoudness: false, checkpoint: checkpoint.path(percentEncoded: false))
        let job = try Job(engine: engine, request: request)
        return try await job.perform { try job.transcribe(input: copy, events: events) }
    }
}

final class Job: @unchecked Sendable {
    struct Request: Encodable {
        var language = "auto"
        var diarize = false
        var glossary: [String] = []
        var evenLoudness = true
        var checkpoint: String?
    }

    private let handle: OpaquePointer

    init(engine: Engine?, request: Request) throws {
        var job: OpaquePointer?
        try check(sillok_job_create(engine?.handle, try encodeJSON(request), &job))
        guard let job else { throw SillokError(code: .internalError, message: "no job") }
        handle = job
    }

    deinit {
        sillok_job_destroy(handle)
    }

    func cancel() {
        sillok_job_cancel(handle)
    }

    func perform<T: Sendable>(_ body: @escaping @Sendable () throws -> T) async throws -> T {
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                let thread = Thread {
                    continuation.resume(with: Result { try body() })
                }
                thread.stackSize = 8 << 20
                thread.qualityOfService = .userInitiated
                thread.start()
            }
        } onCancel: {
            cancel()
        }
    }

    func copy(input: URL, output: URL, events: @escaping @Sendable (TranscriptionEvent) -> Void) throws {
        let callbacks = Callbacks(events: events)
        let context = Unmanaged.passUnretained(callbacks).toOpaque()
        defer { withExtendedLifetime(callbacks) {} }

        try read(input, context: context)
        try check(sillok_job_end_audio(handle))
        try mapCancellation {
            try check(sillok_job_save_audio(handle, output.path(percentEncoded: false), { context, fraction in
                Callbacks.from(context).emit(.saving(fraction))
            }, context))
        }
    }

    func transcribe(input: URL, events: @escaping @Sendable (TranscriptionEvent) -> Void) throws -> TranscriptionResult {
        let callbacks = Callbacks(events: events)
        let context = Unmanaged.passUnretained(callbacks).toOpaque()
        defer { withExtendedLifetime(callbacks) {} }

        try read(input, context: context)
        try check(sillok_job_on_segments(handle, { context, json in
            guard let json, let preview = try? Preview(json: String(cString: json)) else { return }
            Callbacks.from(context).emit(.preview(preview))
        }, context))

        let json = try mapCancellation {
            try takeString { out in
                sillok_job_run(handle, { context, stage, fraction in
                    guard let stage = Stage(rawValue: Int(stage.rawValue)) else { return }
                    Callbacks.from(context).emit(.stage(stage, fraction))
                }, context, out)
            }
        }
        return TranscriptionResult(json: json, transcript: try Transcript(json: json))
    }

    private func read(_ input: URL, context: UnsafeMutableRawPointer) throws {
        try mapCancellation {
            try check(sillok_job_push_file(handle, input.path(percentEncoded: false), { context, fraction in
                Callbacks.from(context).emit(.reading(fraction))
            }, context))
        }
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
        case .preview:
            break
        }
        events(event)
    }
}
