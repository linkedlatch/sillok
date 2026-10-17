import Foundation
import SillokCore

public enum ModelSelection {
    public static let defaultWhisperID = "large-v3-turbo-q5_0"
    public static let vadID = "silero-v6.2.0"
    public static let diarizationID = "nemotron-3-diarization-q8_0"
    public static let runtimeOverhead: Int64 = 1 << 30

    public static func requiredEntries(whisperID: String, useCoreML: Bool, diarization: Bool = false, manifest: ModelManifest) -> [ModelEntry] {
        guard let whisper = manifest.entry(whisperID) else { return [] }
        var entries = [whisper]
        if useCoreML, let encoder = manifest.coreMLEncoder(for: whisper) { entries.append(encoder) }
        if let vad = manifest.entry(vadID) { entries.append(vad) }
        if diarization, let diarizer = manifest.entry(diarizationID) { entries.append(diarizer) }
        return entries
    }

    public static func shouldUseCoreML(whisper: ModelEntry, encoder: ModelEntry?, availableMemory: Int64, countEncoder: Bool) -> Bool {
        guard let encoder else { return false }
        return availableMemory >= whisper.size + (countEncoder ? encoder.size : 0) + runtimeOverhead
    }
}
