import Foundation
import Testing
@testable import SillokCore

@Test func segmentRoundTripsThroughJSON() throws {
    let segment = Segment(
        start: 1.5, end: 3.25, text: "안녕하세요", language: "ko",
        words: [WordTimestamp(word: "안녕하세요", start: 1.5, end: 3.25, probability: 0.9)],
        isLowConfidence: true
    )
    let data = try JSONEncoder().encode(segment)
    #expect(try JSONDecoder().decode(Segment.self, from: data) == segment)
}
