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
    var speakerDiarization = true

    private static let key = "settings"

    init() {}

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let defaults = AppSettings()
        whisperID = try container.decodeIfPresent(String.self, forKey: .whisperID) ?? defaults.whisperID
        useCoreML = try container.decodeIfPresent(Bool.self, forKey: .useCoreML) ?? defaults.useCoreML
        languageMode = try container.decodeIfPresent(LanguageMode.self, forKey: .languageMode) ?? defaults.languageMode
        vocabularyText = try container.decodeIfPresent(String.self, forKey: .vocabularyText) ?? defaults.vocabularyText
        copyOnImport = try container.decodeIfPresent(Bool.self, forKey: .copyOnImport) ?? defaults.copyOnImport
        hasChosenMode = try container.decodeIfPresent(Bool.self, forKey: .hasChosenMode) ?? defaults.hasChosenMode
        speakerDiarization = try container.decodeIfPresent(Bool.self, forKey: .speakerDiarization) ?? defaults.speakerDiarization
    }

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
        TranscriptionOptions(languageMode: languageMode, vocabulary: vocabulary, speakerDiarization: speakerDiarization)
    }
}
