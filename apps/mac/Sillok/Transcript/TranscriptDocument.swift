import Foundation
import Observation
import SillokEngine
import SillokStore

@MainActor
@Observable
final class TranscriptDocument {
    let id: UUID
    private let store: RecordingStore
    private let original: Transcript
    private(set) var edits: TranscriptEdits
    private(set) var transcript: Transcript

    init(id: UUID, store: RecordingStore) throws {
        self.id = id
        self.store = store
        let original = try store.transcript(for: id)
        let edits = try store.edits(for: id)
        self.original = original
        self.edits = edits
        transcript = original.applying(edits)
    }

    func originalText(of segment: Int) -> String {
        original.segments[segment].text
    }

    func setText(_ text: String, of segment: Int) throws {
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        try change { $0.texts[segment] = text == original.segments[segment].text ? nil : text }
    }

    func setSpeaker(_ speaker: Int, of segment: Int) throws {
        try change { $0.speakers[segment] = speaker == original.segments[segment].speaker ? nil : speaker }
    }

    func restore(_ segment: Int) throws {
        try change { $0.restore(segment) }
    }

    func renameSpeakers(_ names: [Int: String]) throws {
        try change { edits in
            for (id, name) in names {
                let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
                let originalName = original.speakers.first { $0.id == id }?.name ?? ""
                edits.speakerNames[id] = name == originalName ? nil : name
            }
        }
    }

    private func change(_ body: (inout TranscriptEdits) -> Void) throws {
        var edits = edits
        body(&edits)
        try store.save(edits, for: id)
        self.edits = edits
        transcript = original.applying(edits)
    }
}
