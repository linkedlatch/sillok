import Foundation
@testable import SillokCore

actor FakeEngine: TranscriptionEngine {
    struct Detection: Sendable {
        var sampleCount: Int
        var allowed: Set<String>?
    }

    var detectedLanguage: String
    var detectionConfidence = 0.9
    var scriptedDetections: [LanguageDetection] = []
    var detections: [Detection] = []
    var transcriptions: [ChunkTranscribeOptions] = []
    var nextSegments: [[Segment]] = []

    init(detectedLanguage: String = "en") {
        self.detectedLanguage = detectedLanguage
    }

    func setDetectedLanguage(_ language: String) { detectedLanguage = language }
    func script(_ detections: [LanguageDetection]) { scriptedDetections = detections }
    func enqueue(_ segments: [Segment]) { nextSegments.append(segments) }

    func loadModel(_ files: ModelFiles, mode: ComputeMode) async throws {}
    func unloadModel() async {}

    func detectLanguage(samples: [Float], allowedLanguages: Set<String>?) async throws -> LanguageDetection {
        detections.append(Detection(sampleCount: samples.count, allowed: allowedLanguages))
        if !scriptedDetections.isEmpty { return scriptedDetections.removeFirst() }
        return LanguageDetection(language: detectedLanguage, confidence: detectionConfidence)
    }

    func tokenCount(_ text: String) async throws -> Int {
        text.split(separator: " ").count
    }

    func transcribe(samples: [Float], options: ChunkTranscribeOptions) async throws -> [Segment] {
        transcriptions.append(options)
        return nextSegments.isEmpty ? [] : nextSegments.removeFirst()
    }
}

struct SilentSource: AudioStreamSource {
    var duration: TimeInterval

    func read(from start: TimeInterval, to end: TimeInterval) async throws -> [Float] {
        let count = Int(((min(end, duration) - max(start, 0)) * Double(PCMFormat.sampleRate)).rounded())
        return [Float](repeating: 0, count: max(count, 0))
    }
}
