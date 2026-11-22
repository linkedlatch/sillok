import Foundation
import Testing

@testable import SillokEngine

let sample = """
    {"version": 1, "duration_ms": 4000,
     "speakers": [{"id": 0, "name": null}, {"id": 1, "name": null}],
     "segments": [
       {"start_ms": 0, "end_ms": 1500, "text": "안녕하세요.", "language": "ko", "speaker": 0,
        "words": [{"text": "안녕하세요.", "start_ms": 0, "end_ms": 1500, "speaker": 0}]},
       {"start_ms": 2000, "end_ms": 3500, "text": "반갑습니다.", "language": "ko", "speaker": 1,
        "words": [{"text": "반갑습니다.", "start_ms": 2000, "end_ms": 3500, "speaker": 1}]}],
     "models": {"transcription": {"name": "whisper", "device": "CPU", "fell_back_to_cpu": false}}}
    """

@Test func exportsTextWithSpeakerLabels() throws {
    let text = try Export.render(sample, as: .txt, speakerLabel: "화자 {n}")
    #expect(text.contains("화자 1"))
    #expect(text.contains("반갑습니다."))
}

@Test func editsAppearInTheExport() throws {
    let original = try Transcript(json: sample)
    var edits = TranscriptEdits()
    edits.texts[0] = "안녕하십니까."
    edits.speakers[1] = 0
    edits.speakerNames = [0: "유나", 1: ""]
    let edited = original.applying(edits)
    #expect(edited.segments[0].words.isEmpty)
    #expect(edited.segments[1].words.map(\.speaker) == [0])
    #expect(edited.speakers.map(\.name) == ["유나", nil])
    let text = try Export.render(edited.json(), as: .txt, speakerLabel: "화자 {n}")
    #expect(text.contains("안녕하십니까."))
    #expect(!text.contains("화자"))
}

@Test func noEditsLeaveTheTranscriptAsItWas() throws {
    let original = try Transcript(json: sample)
    #expect(original.applying(TranscriptEdits()) == original)
    #expect(try Transcript(json: original.json()) == original)
}

@Test func exportsJSONTheLibraryReadsBack() throws {
    let json = try Export.render(sample, as: .json, speakerLabel: "Speaker {n}")
    #expect(try Transcript(json: json) == Transcript(json: sample))
}
