import Foundation

public struct PromptBuilder: Sendable {
    public static let hardTokenLimit = 224

    public var vocabulary: [String]
    public var maxTokens: Int

    public init(vocabulary: [String], maxTokens: Int = 150) {
        self.vocabulary = vocabulary
        self.maxTokens = min(maxTokens, Self.hardTokenLimit)
    }

    public func build(previousText: String?, countTokens: (String) async throws -> Int) async throws -> String? {
        var terms = vocabulary.filter { !$0.isEmpty }
        var termText = terms.joined(separator: ", ")
        while !terms.isEmpty, try await countTokens(termText) > maxTokens {
            terms.removeLast()
            termText = terms.joined(separator: ", ")
        }

        let words = (previousText ?? "").split(separator: " ")
        var low = 0
        var high = words.count
        while low < high {
            let mid = (low + high + 1) / 2
            let candidate = Self.join(termText, words.suffix(mid).joined(separator: " "))
            if try await countTokens(candidate) <= maxTokens {
                low = mid
            } else {
                high = mid - 1
            }
        }
        let prompt = Self.join(termText, words.suffix(low).joined(separator: " "))
        return prompt.isEmpty ? nil : prompt
    }

    private static func join(_ terms: String, _ tail: String) -> String {
        [terms, tail].filter { !$0.isEmpty }.joined(separator: " ")
    }
}
