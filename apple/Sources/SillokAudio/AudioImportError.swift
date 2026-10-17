import Foundation

public enum AudioImportError: Error, Equatable, LocalizedError {
    case unsupportedFormat
    case noAudioTrack

    public var errorDescription: String? {
        switch self {
        case .unsupportedFormat: "unsupported file format"
        case .noAudioTrack: "the file has no audio track"
        }
    }
}
