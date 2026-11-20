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

extension Transcript {
    public static func renamingSpeakers(in json: String, names: [Int: String]) throws -> String {
        guard var document = try JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: Any],
              let speakers = document["speakers"] as? [[String: Any]]
        else {
            throw SillokError(code: .invalidArgument, message: "the transcript has no speakers")
        }
        document["speakers"] = speakers.map { speaker in
            var speaker = speaker
            if let id = speaker["id"] as? Int, let name = names[id] {
                let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
                speaker["name"] = trimmed.isEmpty ? NSNull() : trimmed
            }
            return speaker
        }
        let data = try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys])
        return String(decoding: data, as: UTF8.self)
    }
}
