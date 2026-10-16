import Foundation
import whisper

enum WhisperLog {
    static let configure: Void = {
        guard ProcessInfo.processInfo.environment["SILLOK_WHISPER_LOG"] != "1" else { return }
        whisper_log_set({ level, text, _ in
            if level == GGML_LOG_LEVEL_ERROR, let text { fputs(String(cString: text), stderr) }
        }, nil)
    }()
}
