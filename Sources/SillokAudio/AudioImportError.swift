import Foundation

public enum AudioImportError: Error, Equatable, LocalizedError {
    case unsupportedFormat
    case noAudioTrack

    public var errorDescription: String? {
        switch self {
        case .unsupportedFormat: "지원하지 않는 파일 형식입니다"
        case .noAudioTrack: "오디오 트랙이 없는 파일입니다"
        }
    }
}
