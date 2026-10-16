import Foundation
import SillokCore
import SillokModels

struct AppSettings: Codable, Equatable {
    var whisperID = ModelSelection.defaultWhisperID
    var useCoreML = true
    var languageMode = LanguageMode.auto
    var vocabularyText = ""
    var copyOnImport = false
    var hasChosenMode = false

    private static let key = "settings"

    static func load() -> AppSettings {
        guard let data = UserDefaults.standard.data(forKey: key),
              let settings = try? JSONDecoder().decode(AppSettings.self, from: data) else { return AppSettings() }
        return settings
    }

    func save() {
        if let data = try? JSONEncoder().encode(self) {
            UserDefaults.standard.set(data, forKey: Self.key)
        }
    }

    var vocabulary: [String] {
        vocabularyText.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    var transcriptionOptions: TranscriptionOptions {
        TranscriptionOptions(languageMode: languageMode, vocabulary: vocabulary)
    }
}
