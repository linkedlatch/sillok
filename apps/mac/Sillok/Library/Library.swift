import Foundation
import Observation
import SillokStore

@MainActor
@Observable
final class Library {
    let store: RecordingStore
    private(set) var recordings: [Recording] = []

    init(root: URL = .applicationSupportDirectory.appending(path: "Recordings")) {
        store = RecordingStore(root: root)
        store.removeLeftoverStaging()
        reload()
    }

    func reload() {
        recordings = (try? store.list()) ?? []
    }

    func rename(_ id: UUID, to title: String) {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty else { return }
        try? store.update(id) { $0.title = title }
        reload()
    }

    func delete(_ id: UUID) {
        try? store.delete(id)
        reload()
    }

    func recording(_ id: UUID) -> Recording? {
        recordings.first { $0.id == id }
    }

    func search(_ query: String) async throws -> Set<UUID> {
        try await Self.search(query, in: recordings, of: store)
    }

    @concurrent
    private nonisolated static func search(
        _ query: String, in recordings: [Recording], of store: RecordingStore
    ) async throws -> Set<UUID> {
        let found = try recordings.filter { recording in
            try Task.checkCancellation()
            return recording.title.localizedStandardContains(query)
                || (try? store.editedTranscript(for: recording.id))?.segments
                    .contains { $0.text.localizedStandardContains(query) } == true
        }
        return Set(found.map(\.id))
    }
}
