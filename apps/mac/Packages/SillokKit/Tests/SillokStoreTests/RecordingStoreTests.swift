import Foundation
import Testing

@testable import SillokEngine
@testable import SillokStore

struct RecordingStoreTests {
    let store = RecordingStore(root: FileManager.default.temporaryDirectory.appending(path: "Recordings-\(UUID().uuidString)"))

    func add(_ title: String, at date: Date) throws -> Recording {
        let staging = try store.makeStaging()
        try Data("audio".utf8).write(to: staging.appending(path: RecordingStore.audioName))
        var recording = Recording(title: title, createdAt: date, sourceName: "\(title).m4a",
                                  options: JobOptions(language: "ko", diarize: true), transcriptionModel: "whisper")
        recording.timingsMs = ["copy": 300]
        try store.add(staging: staging, recording: recording)
        return recording
    }

    func save(_ title: String, at date: Date) throws -> Recording {
        let recording = try add(title, at: date)
        try store.finish(recording.id, transcriptJSON: "{\"version\": 1}", durationMs: 1000, timingsMs: [:])
        return try #require(try store.list().first { $0.id == recording.id })
    }

    @Test func listsSavedRecordingsNewestFirst() throws {
        let older = try save("older", at: Date(timeIntervalSince1970: 1_000))
        let newer = try save("newer", at: Date(timeIntervalSince1970: 2_000))
        #expect(try store.list() == [newer, older])
        #expect(try store.transcriptJSON(for: older.id) == "{\"version\": 1}")
        #expect(FileManager.default.fileExists(atPath: store.audio(for: newer.id).path()))
    }

    @Test func recordingsWaitInTheOrderTheyWereAdded() throws {
        let later = try add("later", at: Date(timeIntervalSince1970: 2_000))
        let first = try add("first", at: Date(timeIntervalSince1970: 1_000))
        #expect(try store.pending().map(\.id) == [first.id, later.id])
        #expect(try store.list().isEmpty)
    }

    @Test func copiesARecordingToTranscribeItAgain() throws {
        let recording = try save("meeting", at: Date(timeIntervalSince1970: 1_000))
        let copy = Recording(title: recording.title, sourceName: recording.sourceName,
                             options: JobOptions(language: "en"), transcriptionModel: "qwen")
        try store.add(copyOf: recording.id, as: copy)
        #expect(try store.pending().map(\.id) == [copy.id])
        #expect(try Data(contentsOf: store.audio(for: copy.id)) == Data("audio".utf8))
        #expect(try store.list().map(\.id) == [recording.id])
    }

    @Test func copiesTheDetectedSpeechWithTheRecording() throws {
        let recording = try save("meeting", at: Date(timeIntervalSince1970: 1_000))
        try Data("speech".utf8).write(to: store.speech(for: recording.id))
        let copy = Recording(title: recording.title, sourceName: recording.sourceName,
                             options: JobOptions(language: "en"), transcriptionModel: "qwen")
        try store.add(copyOf: recording.id, as: copy)
        #expect(try Data(contentsOf: store.speech(for: copy.id)) == Data("speech".utf8))
    }

    @Test func finishingARecordingClearsItsError() throws {
        let recording = try add("meeting", at: Date(timeIntervalSince1970: 1_000))
        try store.update(recording.id) { $0.error = "The model failed." }
        #expect(try store.pending().first?.error == "The model failed.")
        try store.finish(recording.id, transcriptJSON: "{\"version\": 1}", durationMs: 1000,
                         timingsMs: ["read": 100, "transcribe": 2000])
        #expect(try store.pending().isEmpty)
        let finished = try #require(try store.list().first)
        #expect(finished.error == nil)
        #expect(finished.durationMs == 1000)
        #expect(finished.transcribedAt != nil)
        #expect(finished.transcriptionModel == "whisper")
        #expect(finished.timingsMs == ["copy": 300, "read": 100, "transcribe": 2000])
    }

    @Test func readsRecordingsSavedBeforeJobInformation() throws {
        let id = UUID()
        let folder = store.folder(for: id)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try Data(#"""
            {"id": "\#(id.uuidString)", "title": "old", "created_at": "2026-11-20T14:36:54Z",
             "source_name": "old.m4a", "duration_ms": 28763,
             "options": {"language": "auto", "diarize": true, "glossary": []}}
            """#.utf8).write(to: folder.appending(path: "meta.json"))
        try Data("{}".utf8).write(to: folder.appending(path: "transcript.json"))
        let recording = try #require(try store.list().first)
        #expect(recording.durationMs == 28763)
        #expect(recording.transcriptionModel == nil)
        #expect(recording.date == recording.createdAt)
    }

    @Test func leavesUnfinishedRecordingsOut() throws {
        let staging = try store.makeStaging()
        #expect(try store.list().isEmpty)
        store.removeLeftoverStaging()
        #expect(!FileManager.default.fileExists(atPath: staging.path()))
    }

    @Test func keepsEditsApartFromTheTranscript() throws {
        let recording = try save("meeting", at: .now)
        #expect(try store.edits(for: recording.id).isEmpty)
        var edits = TranscriptEdits()
        edits.texts[0] = "Hello."
        edits.speakerNames[1] = "Yuna"
        try store.save(edits, for: recording.id)
        #expect(try store.edits(for: recording.id) == edits)
        #expect(try store.transcriptJSON(for: recording.id) == "{\"version\": 1}")
        try store.save(TranscriptEdits(), for: recording.id)
        #expect(!FileManager.default.fileExists(atPath: store.folder(for: recording.id).appending(path: "edits.json").path()))
        try store.delete(recording.id)
        #expect(try store.list().isEmpty)
    }

    @Test func listsNothingWithoutAFolder() throws {
        #expect(try store.list().isEmpty)
    }
}
