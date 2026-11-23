public struct TranscriptEdits: Codable, Sendable, Equatable {
    public var texts: [Int: String] = [:]
    public var speakers: [Int: Int] = [:]
    public var speakerNames: [Int: String] = [:]

    public init() {}

    public var isEmpty: Bool {
        texts.isEmpty && speakers.isEmpty && speakerNames.isEmpty
    }

    public func isEdited(_ segment: Int) -> Bool {
        texts[segment] != nil || speakers[segment] != nil
    }

    public mutating func restore(_ segment: Int) {
        texts[segment] = nil
        speakers[segment] = nil
    }
}

extension Transcript {
    public func applying(_ edits: TranscriptEdits) -> Transcript {
        var edited = self
        for (index, text) in edits.texts where segments.indices.contains(index) {
            edited.segments[index].text = text
            edited.segments[index].words = []
        }
        for (index, speaker) in edits.speakers where segments.indices.contains(index) {
            edited.segments[index].speaker = speaker
            for word in edited.segments[index].words.indices {
                edited.segments[index].words[word].speaker = speaker
            }
        }
        for (id, name) in edits.speakerNames {
            guard let index = speakers.firstIndex(where: { $0.id == id }) else { continue }
            edited.speakers[index].name = name.isEmpty ? nil : name
        }
        return edited
    }
}
