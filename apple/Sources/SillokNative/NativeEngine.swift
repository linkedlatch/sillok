import CSillok
import Foundation
import SillokCore

public final class NativeEngine: @unchecked Sendable {
    let pointer: OpaquePointer
    private let state = NSLock()
    private var loaded: ModelFiles?

    public init() {
        pointer = sillok_engine_new()
    }

    deinit {
        sillok_engine_free(pointer)
    }

    public static var systemInfo: String {
        guard let info = sillok_system_info() else { return "" }
        defer { sillok_free(info) }
        return String(cString: info)
    }

    public var loadedFiles: ModelFiles? {
        state.withLock { loaded }
    }

    public func load(_ files: ModelFiles, mode: ComputeMode) async throws {
        try await NativeCall.run {
            try NativeCall.check(sillok_engine_load(
                self.pointer,
                files.ggml.path,
                mode == .foregroundAccelerated ? files.coreMLEncoder?.path : nil,
                mode == .foregroundAccelerated ? 1 : 0
            ))
        }
        state.withLock { loaded = files }
    }

    public func unload() async {
        try? await NativeCall.run { sillok_engine_unload(self.pointer) }
        state.withLock { loaded = nil }
    }

    public func detectLanguage(samples: [Float], allowedLanguages: Set<String>?) async throws -> LanguageDetection {
        let allowed = try allowedLanguages.map { try NativeCall.json(Array($0).sorted()) }
        return try await NativeCall.run {
            try samples.withUnsafeBufferPointer { buffer in
                try NativeCall.decode(LanguageDetection.self, sillok_engine_detect_language(self.pointer, buffer.baseAddress, buffer.count, allowed))
            }
        }
    }

    public func transcribe(samples: [Float], options: ChunkTranscribeOptions) async throws -> [Segment] {
        let optionsJSON = try NativeCall.json(options)
        return try await withNativeCancellation { cancel in
            try samples.withUnsafeBufferPointer { buffer in
                try NativeCall.decode([Segment].self, sillok_engine_transcribe(self.pointer, buffer.baseAddress, buffer.count, optionsJSON, cancel))
            }
        }
    }
}
