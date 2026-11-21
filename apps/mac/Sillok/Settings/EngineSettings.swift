import Foundation
import SillokEngine

enum EngineSettings {
    static let transcriptionModelKey = "transcriptionModel"
    static let deviceKey = "device"
    static let npuKey = "npu"

    static var transcriptionModel: String? {
        let id = UserDefaults.standard.string(forKey: transcriptionModelKey) ?? ""
        return id.isEmpty ? nil : id
    }

    static func config(modelsDirectory: URL, transcriptionModel: String) -> EngineConfig {
        let defaults = UserDefaults.standard
        return EngineConfig(
            modelsDirectory: modelsDirectory,
            device: EngineConfig.Device(rawValue: defaults.string(forKey: deviceKey) ?? "") ?? .gpu,
            npu: defaults.object(forKey: npuKey) as? Bool ?? true,
            transcriptionModel: transcriptionModel
        )
    }
}
