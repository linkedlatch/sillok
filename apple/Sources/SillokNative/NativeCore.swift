import CSillok
import Foundation
import SillokCore

public struct NativeCore: TranscriptionCore {
    public let engine: NativeEngine
    public let vadModel: URL
    public let diarizationModel: URL?

    public init(engine: NativeEngine, vadModel: URL, diarizationModel: URL? = nil) {
        self.engine = engine
        self.vadModel = vadModel
        self.diarizationModel = diarizationModel
    }

    public func plan(source: AudioStreamSource) async throws -> [AudioChunk] {
        let path = vadModel.path
        return try await NativeCall.run {
            var error: UnsafeMutablePointer<CChar>?
            guard let vad = sillok_vad_new(path, &error) else {
                defer { sillok_free(error) }
                throw NativeError.failed(error.map { String(cString: $0) } ?? "VAD 모델을 불러오지 못했습니다")
            }
            defer { sillok_vad_free(vad) }
            let map = sillok_speech_map_new(vad)
            defer { sillok_speech_map_free(map) }

            let window = Double(Int(sillok_vad_frame_size()) * 1875) / Double(PCMFormat.sampleRate)
            var start: TimeInterval = 0
            while start < source.duration {
                let samples = try source.read(from: start, to: start + window)
                guard !samples.isEmpty else { break }
                try samples.withUnsafeBufferPointer { buffer in
                    try NativeCall.check(sillok_speech_map_append(map, buffer.baseAddress, buffer.count))
                }
                start += window
            }
            return try NativeCall.decode([AudioChunk].self, sillok_speech_map_plan(map, source.duration, nil))
        }
    }

    public func step(_ state: TranscriptionState, chunks: [AudioChunk], source: AudioStreamSource, options: TranscriptionOptions) async throws -> TranscriptionState {
        let engine = engine
        let stateJSON = try NativeCall.json(StepState(state))
        let chunksJSON = try NativeCall.json(chunks)
        let optionsJSON = try NativeCall.json(options)
        let box = SourceBox(source)
        let result = try await withNativeCancellation { cancel in
            try withExtendedLifetime(box) {
                try NativeCall.decode(StepResult.self, sillok_transcribe_step(
                    engine.pointer, stateJSON, chunksJSON, optionsJSON, readAudio,
                    Unmanaged.passUnretained(box).toOpaque(), cancel
                ))
            }
        }
        var next = state
        next.nextChunk = result.state.nextChunk
        next.previousLanguage = result.state.previousLanguage
        next.previousText = result.state.previousText
        next.segments += result.segments
        return next
    }

    public func diarize(_ segments: [Segment], source: AudioStreamSource) async throws -> [Segment] {
        guard let diarizationModel, !segments.isEmpty else { return segments }
        await engine.unload()
        let path = diarizationModel.path
        let segmentsJSON = try NativeCall.json(segments)
        let duration = source.duration
        let box = SourceBox(source)
        return try await withNativeCancellation { cancel in
            var error: UnsafeMutablePointer<CChar>?
            guard let diarizer = sillok_diarizer_new(path, 1, &error) else {
                defer { sillok_free(error) }
                throw NativeError.failed(error.map { String(cString: $0) } ?? "화자 구분 모델을 불러오지 못했습니다")
            }
            defer { sillok_diarizer_free(diarizer) }
            return try withExtendedLifetime(box) {
                try NativeCall.decode([Segment].self, sillok_diarize(
                    diarizer, segmentsJSON, duration, readAudio, Unmanaged.passUnretained(box).toOpaque(), cancel
                ))
            }
        }
    }
}

private struct StepState: Codable {
    var nextChunk: Int
    var previousLanguage: String?
    var previousText: String?

    init(_ state: TranscriptionState) {
        nextChunk = state.nextChunk
        previousLanguage = state.previousLanguage
        previousText = state.previousText
    }
}

private struct StepResult: Decodable {
    var state: StepState
    var segments: [Segment]
}

private final class SourceBox: @unchecked Sendable {
    let source: AudioStreamSource

    init(_ source: AudioStreamSource) {
        self.source = source
    }
}

private let readAudio: sillok_read_audio = { context, start, end, output in
    guard let context else { return }
    let box = Unmanaged<SourceBox>.fromOpaque(context).takeUnretainedValue()
    let samples = (try? box.source.read(from: start, to: end)) ?? []
    samples.withUnsafeBufferPointer { sillok_buffer_assign(output, $0.baseAddress, $0.count) }
}
