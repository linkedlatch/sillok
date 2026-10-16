import Foundation

public enum LanguageMode: Codable, Hashable, Sendable {
    case auto
    case fixed(String)
    case koreanEnglish
}
