import Foundation
import SillokCore
import whisper

final class WhisperContext: @unchecked Sendable {
    private static let queue = DispatchQueue(label: "sillok.whisper", qos: .userInitiated)

    private let pointer: OpaquePointer
    private let threads: Int32
    private let stagingDirectory: URL

    private init(pointer: OpaquePointer, threads: Int32, stagingDirectory: URL) {
        self.pointer = pointer
        self.threads = threads
        self.stagingDirectory = stagingDirectory
    }

    deinit {
        whisper_free(pointer)
        try? FileManager.default.removeItem(at: stagingDirectory)
    }

    static func run<T: Sendable>(_ work: @escaping @Sendable () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            queue.async { continuation.resume(with: Result { try work() }) }
        }
    }

    static func load(_ files: ModelFiles, mode: ComputeMode) throws -> WhisperContext {
        _ = WhisperLog.configure
        let staging = try stage(files, includeCoreML: mode == .foregroundAccelerated)
        let stagingDirectory = staging.deletingLastPathComponent()

        var params = whisper_context_default_params()
        params.use_gpu = mode == .foregroundAccelerated
        guard let pointer = staging.path.withCString({ whisper_init_from_file_with_params($0, params) }) else {
            try? FileManager.default.removeItem(at: stagingDirectory)
            throw WhisperEngineError.loadFailed(files.ggml.lastPathComponent)
        }
        let threads = Int32(min(4, ProcessInfo.processInfo.activeProcessorCount))
        return WhisperContext(pointer: pointer, threads: threads, stagingDirectory: stagingDirectory)
    }

    private static func stage(_ files: ModelFiles, includeCoreML: Bool) throws -> URL {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("sillok-load-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let name = files.ggml.lastPathComponent
        let ggml = dir.appendingPathComponent(name)
        try FileManager.default.createSymbolicLink(at: ggml, withDestinationURL: files.ggml)
        if includeCoreML, let encoder = files.coreMLEncoder {
            try FileManager.default.createSymbolicLink(
                at: dir.appendingPathComponent(ModelFiles.coreMLEncoderName(forGGML: name)),
                withDestinationURL: encoder
            )
        }
        return ggml
    }

    func transcribe(_ samples: [Float], options: ChunkTranscribeOptions, abort: AbortFlag) throws -> [Segment] {
        if abort.isSet { throw CancellationError() }
        var params = whisper_full_default_params(options.beamSize > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY)
        params.n_threads = threads
        params.no_context = true
        params.single_segment = false
        params.print_special = false
        params.print_progress = false
        params.print_realtime = false
        params.print_timestamps = false
        params.token_timestamps = true
        params.detect_language = false
        params.temperature = 0
        params.temperature_inc = 0.2
        params.entropy_thold = 2.4
        params.logprob_thold = -1
        params.no_speech_thold = 0.6
        params.beam_search.beam_size = Int32(options.beamSize)
        params.abort_callback = { data in
            Unmanaged<AbortFlag>.fromOpaque(data!).takeUnretainedValue().isSet
        }
        params.abort_callback_user_data = Unmanaged.passUnretained(abort).toOpaque()

        let status = options.language.withCString { language in
            withOptionalCString(options.prompt) { prompt in
                params.language = language
                params.initial_prompt = prompt
                return samples.withUnsafeBufferPointer {
                    whisper_full(pointer, params, $0.baseAddress, Int32($0.count))
                }
            }
        }
        if abort.isSet { throw CancellationError() }
        guard status == 0 else { throw WhisperEngineError.inferenceFailed(status) }
        return segments(language: options.language)
    }

    private func segments(language: String) -> [Segment] {
        let eot = whisper_token_eot(pointer)
        return (0..<whisper_full_n_segments(pointer)).map { i in
            let tokens = (0..<whisper_full_n_tokens(pointer, i))
                .map { (index: $0, data: whisper_full_get_token_data(pointer, i, $0)) }
                .filter { $0.data.id < eot }
            let logprobs = tokens.map(\.data.plog)
            let words = TokenWordAssembler.words(from: tokens.map { token in
                let text = whisper_full_get_token_text(pointer, i, token.index)!
                return TokenPiece(
                    bytes: Array(UnsafeBufferPointer(start: UnsafeRawPointer(text).assumingMemoryBound(to: UInt8.self), count: strlen(text))),
                    start: TimeInterval(token.data.t0) / 100,
                    end: TimeInterval(token.data.t1) / 100,
                    probability: token.data.p
                )
            })
            return Segment(
                start: TimeInterval(whisper_full_get_segment_t0(pointer, i)) / 100,
                end: TimeInterval(whisper_full_get_segment_t1(pointer, i)) / 100,
                text: String(cString: whisper_full_get_segment_text(pointer, i)).trimmingCharacters(in: .whitespaces),
                language: language,
                words: words.isEmpty ? nil : words,
                avgLogprob: logprobs.isEmpty ? nil : logprobs.reduce(0, +) / Float(logprobs.count),
                noSpeechProb: whisper_full_get_segment_no_speech_prob(pointer, i)
            )
        }
    }
}

private func withOptionalCString<T>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> T) -> T {
    guard let string else { return body(nil) }
    return string.withCString { body($0) }
}
