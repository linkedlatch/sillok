import CSillok
import Foundation
import SillokCore

public enum NativeExport {
    public enum Format: String, Sendable {
        case txt
        case srt
    }

    public static func render(_ segments: [Segment], as format: Format) throws -> String {
        try NativeCall.decode(String.self, sillok_export(try NativeCall.json(segments), format.rawValue))
    }
}
