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
}
