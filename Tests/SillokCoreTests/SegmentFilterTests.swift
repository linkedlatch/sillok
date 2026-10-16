import Testing
@testable import SillokCore

private func segment(noSpeech: Float?, logprob: Float?, ratio: Float? = nil) -> Segment {
    Segment(start: 0, end: 1, text: "x", language: "ko", avgLogprob: logprob, noSpeechProb: noSpeech, compressionRatio: ratio)
}

@Test func silenceHallucinationIsDropped() {
    #expect(SegmentFilter().apply([segment(noSpeech: 0.7, logprob: -1.2)]).isEmpty)
}

@Test func highNoSpeechWithConfidentTextIsKept() {
    let kept = SegmentFilter().apply([segment(noSpeech: 0.7, logprob: -0.5)])
    #expect(kept.count == 1 && !kept[0].isLowConfidence)
}

@Test(arguments: [
    (Float(0.1), Float(-1.2), nil as Float?),
    (Float(0.1), Float(-0.3), Float(2.5)),
])
func weakSegmentIsKeptButMarkedLowConfidence(noSpeech: Float, logprob: Float, ratio: Float?) {
    let kept = SegmentFilter().apply([segment(noSpeech: noSpeech, logprob: logprob, ratio: ratio)])
    #expect(kept.count == 1 && kept[0].isLowConfidence)
}

@Test func thresholdsAreExclusive() {
    let kept = SegmentFilter().apply([segment(noSpeech: 0.6, logprob: -1.0, ratio: 2.4)])
    #expect(kept.count == 1 && !kept[0].isLowConfidence)
}
