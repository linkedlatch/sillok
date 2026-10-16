import Foundation

public enum ModelError: Error, Equatable, LocalizedError {
    case checksumMismatch(String)
    case sizeMismatch(String)
    case allSourcesFailed(String)
    case unknownModelFile
    case invalidArchive(String)
    case insufficientDiskSpace(required: Int64, available: Int64)

    public var errorDescription: String? {
        switch self {
        case .checksumMismatch(let name): "\(name) 파일의 체크섬이 일치하지 않습니다"
        case .sizeMismatch(let name): "\(name) 파일의 크기가 일치하지 않습니다"
        case .allSourcesFailed(let name): "\(name) 파일을 내려받지 못했습니다. 네트워크 상태를 확인해 주세요"
        case .unknownModelFile: "앱이 지원하는 모델 파일이 아닙니다"
        case .invalidArchive(let reason): "압축 파일을 풀 수 없습니다: \(reason)"
        case .insufficientDiskSpace(let required, let available):
            "저장 공간이 부족합니다 (필요 \(ByteCountFormatter.string(fromByteCount: required, countStyle: .file)), 남은 공간 \(ByteCountFormatter.string(fromByteCount: available, countStyle: .file)))"
        }
    }
}
