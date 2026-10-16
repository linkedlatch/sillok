import Testing
@testable import SillokCore

@Sendable private func countWords(_ text: String) async throws -> Int {
    text.split(separator: " ").count
}

@Test func promptCombinesTermsAndTailOfPreviousText() async throws {
    let prompt = try await PromptBuilder(vocabulary: ["GOT", "fprintf"], maxTokens: 6)
        .build(previousText: "one two three four five six", countTokens: countWords)
    #expect(prompt == "GOT, fprintf three four five six")
}

@Test func promptWithoutPreviousTextIsTermsOnly() async throws {
    let prompt = try await PromptBuilder(vocabulary: ["GOT"], maxTokens: 150).build(previousText: nil, countTokens: countWords)
    #expect(prompt == "GOT")
}

@Test func emptyPromptIsNil() async throws {
    #expect(try await PromptBuilder(vocabulary: [], maxTokens: 150).build(previousText: nil, countTokens: countWords) == nil)
}

@Test func termsBeyondLimitAreDroppedFromTheEnd() async throws {
    let prompt = try await PromptBuilder(vocabulary: ["a", "b", "c", "d"], maxTokens: 2)
        .build(previousText: "x y", countTokens: countWords)
    #expect(prompt == "a, b")
}

@Test func maxTokensIsClampedToWhisperLimit() {
    #expect(PromptBuilder(vocabulary: [], maxTokens: 1000).maxTokens == 224)
}
