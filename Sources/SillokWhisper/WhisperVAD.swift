import Foundation
import SillokCore
import whisper

public final class WhisperVAD: VoiceActivityDetector, @unchecked Sendable {
    public let frameSize = 512
    private let pointer: OpaquePointer
    private let queue = DispatchQueue(label: "sillok.vad", qos: .userInitiated)

    public init(modelURL: URL) throws {
        _ = WhisperLog.configure
        var params = whisper_vad_default_context_params()
        params.use_gpu = false
        params.n_threads = 2
        guard let pointer = modelURL.path.withCString({ whisper_vad_init_from_file_with_params($0, params) }) else {
            throw WhisperEngineError.loadFailed(modelURL.lastPathComponent)
        }
        self.pointer = pointer
    }

    deinit {
        whisper_vad_free(pointer)
    }

    public func reset() async {
        await withCheckedContinuation { continuation in
            queue.async {
                whisper_vad_reset_state(self.pointer)
                continuation.resume()
            }
        }
    }

    public func speechProbabilities(_ samples: [Float]) async throws -> [Float] {
        try await withCheckedThrowingContinuation { continuation in
            queue.async {
                continuation.resume(with: Result { try self.detect(samples) })
            }
        }
    }

    private func detect(_ samples: [Float]) throws -> [Float] {
        let ok = samples.withUnsafeBufferPointer {
            whisper_vad_detect_speech_no_reset(pointer, $0.baseAddress, Int32($0.count))
        }
        guard ok else { throw WhisperEngineError.inferenceFailed(-1) }
        let count = Int(whisper_vad_n_probs(pointer))
        guard count == (samples.count + frameSize - 1) / frameSize else {
            throw WhisperEngineError.inferenceFailed(-2)
        }
        return Array(UnsafeBufferPointer(start: whisper_vad_probs(pointer), count: count))
    }
}
