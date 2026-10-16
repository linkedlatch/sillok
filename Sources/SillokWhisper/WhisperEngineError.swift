import Foundation

public enum WhisperEngineError: Error, Equatable {
    case modelNotLoaded
    case loadFailed(String)
    case inferenceFailed(Int32)
}
