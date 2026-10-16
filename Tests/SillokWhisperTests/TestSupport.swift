import Darwin
import Foundation
import SillokAudio
import SillokCore

enum TestSupport {
    static let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static let tinyModel = repoRoot.appendingPathComponent("models/ggml-tiny.bin")
    static let tinyEncoder = repoRoot.appendingPathComponent("models/ggml-tiny-encoder.mlmodelc")

    static let vadModel = repoRoot.appendingPathComponent("models/ggml-silero-v6.2.0.bin")

    static var hasTinyModel: Bool {
        FileManager.default.fileExists(atPath: tinyModel.path)
    }

    static var hasVADModel: Bool {
        FileManager.default.fileExists(atPath: vadModel.path)
    }

    static func speech(_ text: String, voice: String) async throws -> [Float] {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("sillok-speech-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let aiff = dir.appendingPathComponent("speech.aiff")
        let wav = dir.appendingPathComponent("speech.wav")
        try run("/usr/bin/say", ["-v", voice, "-o", aiff.path, text])
        try run("/usr/bin/afconvert", ["-f", "WAVE", "-d", "LEI16", aiff.path, wav.path])
        let source = try await AssetAudioSource(url: wav)
        return try await source.read(from: 0, to: source.duration)
    }

    static func footprintMB() -> Double {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
        let result = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        return result == KERN_SUCCESS ? Double(info.phys_footprint) / 1_048_576 : 0
    }

    private static func run(_ tool: String, _ arguments: [String]) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = arguments
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw NSError(domain: "TestSupport", code: Int(process.terminationStatus))
        }
    }
}
