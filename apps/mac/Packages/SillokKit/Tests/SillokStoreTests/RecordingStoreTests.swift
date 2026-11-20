import Foundation
import Testing

@testable import SillokEngine
@testable import SillokStore

struct RecordingStoreTests {
    let store = RecordingStore(root: FileManager.default.temporaryDirectory.appending(path: "Recordings-\(UUID().uuidString)"))

    func save(_ title: String, at date: Date) throws -> Recording {
        let staging = try store.makeStaging()
        try Data("audio".utf8).write(to: staging.appending(path: RecordingStore.audioName))
        let recording = Recording(title: title, createdAt: date, sourceName: "\(title).m4a", durationMs: 1000,
                                  options: JobOptions(language: "ko", diarize: true))
        try store.commit(staging: staging, recording: recording, transcriptJSON: "{\"version\": 1}")
        return recording
    }

    @Test func listsSavedRecordingsNewestFirst() throws {
        let older = try save("older", at: Date(timeIntervalSince1970: 1_000))
        let newer = try save("newer", at: Date(timeIntervalSince1970: 2_000))
        #expect(try store.list() == [newer, older])
        #expect(try store.transcriptJSON(for: older.id) == "{\"version\": 1}")
        #expect(FileManager.default.fileExists(atPath: store.audio(for: newer.id).path()))
    }

    @Test func leavesUnfinishedRecordingsOut() throws {
        let staging = try store.makeStaging()
        #expect(try store.list().isEmpty)
        store.removeLeftoverStaging()
        #expect(!FileManager.default.fileExists(atPath: staging.path()))
    }

    @Test func savesTranscriptsAndDeletesRecordings() throws {
        let recording = try save("meeting", at: .now)
        try store.saveTranscript("{\"version\": 2}", for: recording.id)
        #expect(try store.transcriptJSON(for: recording.id) == "{\"version\": 2}")
        try store.delete(recording.id)
        #expect(try store.list().isEmpty)
    }

    @Test func listsNothingWithoutAFolder() throws {
        #expect(try store.list().isEmpty)
    }
}
