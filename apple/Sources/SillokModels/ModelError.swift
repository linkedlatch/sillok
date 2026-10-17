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
        case .checksumMismatch(let name): "\(name) failed checksum verification"
        case .sizeMismatch(let name): "\(name) has an unexpected size"
        case .allSourcesFailed(let name): "\(name) could not be downloaded from any source"
        case .unknownModelFile: "the file is not a supported model"
        case .invalidArchive(let reason): "the archive could not be extracted: \(reason)"
        case .insufficientDiskSpace(let required, let available):
            "not enough disk space (\(required) bytes required, \(available) bytes available)"
        }
    }
}
