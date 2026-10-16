import Foundation
import SillokCore

struct TokenPiece: Equatable {
    var bytes: [UInt8]
    var start: TimeInterval
    var end: TimeInterval
    var probability: Float
}

enum TokenWordAssembler {
    static func words(from tokens: [TokenPiece]) -> [WordTimestamp] {
        var words: [WordTimestamp] = []
        var current: [TokenPiece] = []

        func flush() {
            guard let first = current.first, let last = current.last else { return }
            words.append(WordTimestamp(
                word: String(decoding: current.flatMap(\.bytes), as: UTF8.self),
                start: first.start,
                end: max(last.end, first.start),
                probability: current.map(\.probability).reduce(0, +) / Float(current.count)
            ))
            current = []
        }

        for token in tokens where !token.bytes.isEmpty {
            if token.bytes.first == UInt8(ascii: " ") { flush() }
            current.append(token)
        }
        flush()
        return words
    }
}
