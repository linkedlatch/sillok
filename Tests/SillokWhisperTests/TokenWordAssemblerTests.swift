import Foundation
import Testing
@testable import SillokWhisper

private func piece(_ bytes: [UInt8], _ start: TimeInterval, _ end: TimeInterval, _ p: Float = 0.8) -> TokenPiece {
    TokenPiece(bytes: bytes, start: start, end: end, probability: p)
}

@Test func tokensJoinIntoWordsAtLeadingSpace() {
    let words = TokenWordAssembler.words(from: [
        piece(Array(" The".utf8), 0.0, 0.2),
        piece(Array(" quick".utf8), 0.2, 0.5),
        piece(Array(" bro".utf8), 0.5, 0.6),
        piece(Array("wn".utf8), 0.6, 0.8),
        piece(Array(".".utf8), 0.8, 0.85),
    ])
    #expect(words.map(\.word) == [" The", " quick", " brown."])
    #expect(words[2].start == 0.5 && words[2].end == 0.85)
}

@Test func koreanCharacterSplitAcrossTokensIsDecodedWhole() {
    let bytes = Array(" 안녕".utf8)
    let words = TokenWordAssembler.words(from: [
        piece(Array(bytes[0..<3]), 1.0, 1.1, 0.6),
        piece(Array(bytes[3...]), 1.1, 1.4, 1.0),
        piece(Array(" 하세요".utf8), 1.4, 1.9),
    ])
    #expect(words.map(\.word) == [" 안녕", " 하세요"])
    #expect(words[0].probability == 0.8)
}

@Test func emptyTokensAreIgnored() {
    #expect(TokenWordAssembler.words(from: [piece([], 0, 1)]).isEmpty)
}
