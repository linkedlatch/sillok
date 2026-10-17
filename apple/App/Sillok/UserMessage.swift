import Foundation
import SillokAudio
import SillokModels

enum UserMessage {
    static let speakerFormat = "화자 {speaker}"

    static func describe(_ error: Error) -> String {
        switch error {
        case let error as ModelError: describe(error)
        case let error as AudioImportError: describe(error)
        default: error.localizedDescription
        }
    }

    private static func describe(_ error: ModelError) -> String {
        switch error {
        case .checksumMismatch(let name): "\(name) 파일이 손상되었습니다. 다시 받아 주세요"
        case .sizeMismatch(let name): "\(name) 파일의 크기가 일치하지 않습니다"
        case .allSourcesFailed(let name): "\(name) 파일을 내려받지 못했습니다. 네트워크 상태를 확인해 주세요"
        case .unknownModelFile: "앱이 지원하는 모델 파일이 아닙니다"
        case .invalidArchive(let reason): "압축 파일을 풀 수 없습니다: \(reason)"
        case .insufficientDiskSpace(let required, let available):
            "저장 공간이 부족합니다 (필요 \(bytes(required)), 남은 공간 \(bytes(available)))"
        }
    }

    private static func describe(_ error: AudioImportError) -> String {
        switch error {
        case .unsupportedFormat: "지원하지 않는 파일 형식입니다"
        case .noAudioTrack: "오디오 트랙이 없는 파일입니다"
        }
    }

    private static func bytes(_ count: Int64) -> String {
        ByteCountFormatter.string(fromByteCount: count, countStyle: .file)
    }
}
