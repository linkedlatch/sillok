import whisper

public enum WhisperBuildInfo {
    public static var systemInfo: String {
        String(cString: whisper_print_system_info())
    }

    public static var version: String {
        String(cString: whisper_version())
    }

    public static var backendNames: [String] {
        (0..<ggml_backend_reg_count()).map { String(cString: ggml_backend_reg_name(ggml_backend_reg_get($0))) }
    }
}
