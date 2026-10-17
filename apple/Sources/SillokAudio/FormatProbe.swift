import Foundation

public enum FormatProbe {
    public static let supportedExtensions: Set<String> = ["mp4", "mov", "m4a", "wav", "mp3", "caf"]

    public static func isSupported(_ url: URL) -> Bool {
        supportedExtensions.contains(url.pathExtension.lowercased())
    }
}
