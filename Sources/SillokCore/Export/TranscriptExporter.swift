import Foundation

public enum TranscriptExporter {
    public static func txt(_ segments: [Segment]) -> String {
        segments.map(\.text).joined(separator: "\n") + "\n"
    }

    public static func srt(_ segments: [Segment]) -> String {
        segments.enumerated().map { index, segment in
            "\(index + 1)\n\(timestamp(segment.start)) --> \(timestamp(segment.end))\n\(segment.text)\n"
        }.joined(separator: "\n")
    }

    public static func timestamp(_ seconds: TimeInterval) -> String {
        let milliseconds = Int((max(seconds, 0) * 1000).rounded())
        return String(format: "%02d:%02d:%02d,%03d", milliseconds / 3_600_000, milliseconds / 60_000 % 60, milliseconds / 1000 % 60, milliseconds % 1000)
    }

    public static func clock(_ seconds: TimeInterval) -> String {
        let total = Int(max(seconds, 0))
        return total >= 3600
            ? String(format: "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60)
            : String(format: "%02d:%02d", total / 60, total % 60)
    }
}
