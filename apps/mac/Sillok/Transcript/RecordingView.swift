import SillokEngine
import SillokStore
import SwiftUI

struct RecordingView: View {
    let recording: Recording
    @Environment(Library.self) private var library
    @State private var transcript: Transcript?
    @State private var failure: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 4) {
                Text(recording.title)
                    .font(.title2.bold())
                Text("\(recording.createdAt.formatted(date: .abbreviated, time: .shortened)) · \(Timestamp.text(recording.durationMs))")
                    .foregroundStyle(.secondary)
            }
            .padding(24)

            if let transcript {
                List(Array(transcript.segments.enumerated()), id: \.offset) { _, segment in
                    HStack(alignment: .firstTextBaseline, spacing: 12) {
                        Text(Timestamp.text(segment.startMs))
                            .font(.callout.monospacedDigit())
                            .foregroundStyle(.secondary)
                        if let speaker = segment.speaker {
                            Text(name(of: speaker, in: transcript))
                                .font(.callout.weight(.semibold))
                                .foregroundStyle(.tint)
                        }
                        Text(segment.text)
                            .textSelection(.enabled)
                    }
                }
                .listStyle(.plain)
            } else if let failure {
                ContentUnavailableView("The Transcript Cannot Be Read", systemImage: "exclamationmark.triangle",
                                       description: Text(failure))
            }
        }
        .task(id: recording.id) {
            do {
                transcript = try Transcript(json: library.store.transcriptJSON(for: recording.id))
                failure = nil
            } catch {
                transcript = nil
                failure = error.localizedDescription
            }
        }
    }

    private func name(of speaker: Int, in transcript: Transcript) -> String {
        transcript.speakers.first { $0.id == speaker }?.name ?? String(localized: "Speaker \(speaker + 1)")
    }
}
