import AVFoundation
import Foundation

enum Fixtures {
    static let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static func localSample(_ name: String) -> URL {
        repoRoot.appendingPathComponent(name)
    }

    static func hasLocalSample(_ name: String) -> Bool {
        FileManager.default.fileExists(atPath: localSample(name).path)
    }

    static let toneDuration: TimeInterval = 5

    static func tone(_ ext: String) throws -> URL {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("sillok-fixtures-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let wav = dir.appendingPathComponent("tone.wav")
        try writeStereoTone(to: wav)
        let target = dir.appendingPathComponent("tone.\(ext)")
        switch ext {
        case "wav":
            return wav
        case "m4a":
            try run("/usr/bin/afconvert", ["-f", "m4af", "-d", "aac", wav.path, target.path])
        case "mp4":
            try run("/usr/bin/afconvert", ["-f", "mp4f", "-d", "aac", wav.path, target.path])
        case "caf":
            try run("/usr/bin/afconvert", ["-f", "caff", "-d", "LEI16", wav.path, target.path])
        case "mov":
            let m4a = dir.appendingPathComponent("tone.m4a")
            try run("/usr/bin/afconvert", ["-f", "m4af", "-d", "aac", wav.path, m4a.path])
            try run("/usr/bin/avconvert", ["-s", m4a.path, "-o", target.path, "-p", "PresetPassthrough"])
        default:
            FileManager.default.createFile(atPath: target.path, contents: Data(count: 1024))
        }
        return target
    }

    private static func writeStereoTone(to url: URL) throws {
        let rate = 44_100.0
        let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2)!
        let frames = AVAudioFrameCount(rate * toneDuration)
        let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frames)!
        buffer.frameLength = frames
        let left = buffer.floatChannelData![0]
        let right = buffer.floatChannelData![1]
        for i in 0..<Int(frames) {
            left[i] = 0.25 * sin(2 * .pi * 440 * Float(i) / Float(rate))
            right[i] = 0
        }
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: rate,
            AVNumberOfChannelsKey: 2,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
        ]
        let file = try AVAudioFile(forWriting: url, settings: settings)
        try file.write(from: buffer)
    }

    private static func run(_ tool: String, _ arguments: [String]) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw NSError(domain: "Fixtures", code: Int(process.terminationStatus))
        }
    }
}

func rms(_ samples: ArraySlice<Float>) -> Float {
    guard !samples.isEmpty else { return 0 }
    return (samples.reduce(0) { $0 + $1 * $1 } / Float(samples.count)).squareRoot()
}
