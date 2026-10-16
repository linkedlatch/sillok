import Foundation

public enum LanguageMode: Codable, Hashable, Sendable {
    case auto
    case fixed(String)
    case koreanEnglish

    public var allowedLanguages: Set<String>? {
        switch self {
        case .auto: nil
        case .fixed(let code): [code]
        case .koreanEnglish: ["ko", "en"]
        }
    }
}
