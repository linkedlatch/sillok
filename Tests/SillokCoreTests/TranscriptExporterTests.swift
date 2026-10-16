import Testing
@testable import SillokCore

private let segments = [
    Segment(start: 0.5, end: 2.25, text: "안녕하세요", language: "ko"),
    Segment(start: 3661.001, end: 3662.9996, text: "Hello", language: "en"),
]

@Test func srtUsesCommaMillisecondsAndSequentialIndices() {
    #expect(TranscriptExporter.srt(segments) == """
    1
    00:00:00,500 --> 00:00:02,250
    안녕하세요

    2
    01:01:01,001 --> 01:01:03,000
    Hello

    """)
}

@Test func txtIsOneLinePerSegment() {
    #expect(TranscriptExporter.txt(segments) == "안녕하세요\nHello\n")
}

@Test func clockFormatsShortAndLongDurations() {
    #expect(TranscriptExporter.clock(75) == "01:15")
    #expect(TranscriptExporter.clock(3725) == "1:02:05")
}
