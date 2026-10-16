import Foundation
import Testing
@testable import SillokAudio
@testable import SillokCore

private let rate = Double(PCMFormat.sampleRate)

@Test(arguments: ["wav", "m4a", "mp4", "mov", "caf"])
func decodesSupportedFormatToMono16k(ext: String) async throws {
    let source = try await AssetAudioSource(url: Fixtures.tone(ext))
    #expect(abs(source.duration - Fixtures.toneDuration) < (ext == "mov" ? 0.05 : 0.01))

    let samples = try await source.read(from: 0, to: source.duration)
    #expect(samples.count == Int((source.duration * rate).rounded()))
    #expect(rms(samples[...]) > 0.05)
}

@Test(arguments: ["wav", "m4a"])
func rangeReadMatchesSameSliceOfFullRead(ext: String) async throws {
    let source = try await AssetAudioSource(url: Fixtures.tone(ext))
    let full = try await source.read(from: 0, to: source.duration)
    let part = try await source.read(from: 1.25, to: 3.5)

    #expect(part.count == 36_000)
    let expected = full[20_000..<56_000]
    let maxDiff = zip(part, expected).map { abs($0 - $1) }.max() ?? .infinity
    #expect(maxDiff < 1e-3)
}

@Test func readBeyondDurationIsClamped() async throws {
    let source = try await AssetAudioSource(url: Fixtures.tone("wav"))
    let tail = try await source.read(from: 4.5, to: 10)
    #expect(tail.count == 8_000)
    #expect(try await source.read(from: 6, to: 7).isEmpty)
}

@Test(arguments: ["mkv", "webm", "ogg"])
func rejectsUnsupportedContainer(ext: String) async throws {
    let url = try Fixtures.tone(ext)
    await #expect(throws: AudioImportError.unsupportedFormat) {
        try await AssetAudioSource(url: url)
    }
}

@Test(.enabled(if: Fixtures.hasLocalSample("sample.mp3")))
func decodesLocalMP3Sample() async throws {
    let source = try await AssetAudioSource(url: Fixtures.localSample("sample.mp3"))
    let samples = try await source.read(from: 0, to: min(source.duration, 10))
    #expect(!samples.isEmpty)
    #expect(rms(samples[...]) > 0)
}

@Test(.enabled(if: Fixtures.hasLocalSample("서강대길.m4a")))
func sampleRecordingDecodesFullLengthInWindows() async throws {
    let source = try await AssetAudioSource(url: Fixtures.localSample("서강대길.m4a"))
    #expect(abs(source.duration - 2499.84) < 0.01)

    var total = 0
    var start: TimeInterval = 0
    while start < source.duration {
        total += try await source.read(from: start, to: start + 30).count
        start += 30
    }
    #expect(total == Int((source.duration * rate).rounded()))
}
