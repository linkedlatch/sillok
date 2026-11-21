import SillokEngine
import SwiftUI

struct SpeakerNamesView: View {
    let transcript: Transcript
    let save: ([Int: String]) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var names: [Int: String] = [:]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Form {
                Section {
                    ForEach(transcript.speakers) { speaker in
                        VStack(alignment: .leading, spacing: 4) {
                            TextField(Speakers.label(speaker.id), text: binding(speaker.id),
                                      prompt: Text(Speakers.label(speaker.id)))
                            if let sample = sample(of: speaker.id) {
                                Text(sample)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                        }
                    }
                } header: {
                    Text("Speaker Names")
                }
            }
            .formStyle(.grouped)

            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Save") {
                    save(names)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
            .padding([.horizontal, .bottom], 20)
        }
        .frame(width: 420)
        .onAppear {
            names = Dictionary(uniqueKeysWithValues: transcript.speakers.map { ($0.id, $0.name ?? "") })
        }
    }

    private func binding(_ id: Int) -> Binding<String> {
        Binding(get: { names[id] ?? "" }, set: { names[id] = $0 })
    }

    private func sample(of speaker: Int) -> String? {
        transcript.segments.first { $0.speaker == speaker }?.text
    }
}

enum Speakers {
    static func label(_ id: Int) -> String {
        String(localized: "Speaker \(id + 1)")
    }

    static func name(_ id: Int, in transcript: Transcript) -> String {
        transcript.speakers.first { $0.id == id }?.name ?? label(id)
    }
}
