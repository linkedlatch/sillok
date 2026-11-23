import CSillok
import Foundation

public enum ExportFormat: String, Sendable, CaseIterable {
    case txt
    case json
}

public enum Export {
    public static func render(_ transcriptJSON: String, as format: ExportFormat, speakerLabel: String) throws -> String {
        let options = try encodeJSON(["speaker_label": speakerLabel])
        return try takeString { sillok_export(transcriptJSON, format.rawValue, options, $0) }
    }
}
