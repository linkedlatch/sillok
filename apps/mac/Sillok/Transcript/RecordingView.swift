import SillokEngine
import SillokStore
import SwiftUI
import UniformTypeIdentifiers

struct RecordingView: View {
    let recording: Recording
    @Environment(Library.self) private var library
    @Environment(Player.self) private var player
    @State private var json: String?
    @State private var transcript: Transcript?
    @State private var failure: String?
    @State private var renaming = false
    @State private var export: (document: TextDocument, type: UTType)?
    @State private var problem: (title: LocalizedStringKey, message: String)?
    @State private var query = ""
    @State private var showingInfo = false
    @FocusState private var searching: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 4) {
                Text(recording.title)
                    .font(.title2.bold())
                Text("\(recording.date.formatted(date: .abbreviated, time: .shortened)) · \(Timestamp.text(recording.durationMs ?? 0))")
                    .foregroundStyle(.secondary)
            }
            .padding(24)

            if let transcript {
                segments(transcript)
                Divider()
                PlayerBar()
            } else if let failure {
                ContentUnavailableView("The Transcript Cannot Be Read", systemImage: "exclamationmark.triangle",
                                       description: Text(failure))
            }
        }
        .toolbar {
            if let transcript, !transcript.speakers.isEmpty {
                Button("Speaker Names", systemImage: "person.2") { renaming = true }
            }
            Button("Copy Text", systemImage: "doc.on.doc") { copyText() }
                .disabled(json == nil)
            Menu("Export", systemImage: "square.and.arrow.up") {
                Button("Text") { prepareExport(.txt) }
                Button("JSON") { prepareExport(.json) }
            }
            .disabled(json == nil)
            Button("Info", systemImage: "info.circle") { showingInfo = true }
                .popover(isPresented: $showingInfo) {
                    RecordingInfoView(recording: recording, transcript: transcript)
                }
        }
        .searchable(text: $query, placement: .toolbar, prompt: Text("Search Transcript"))
        .searchFocused($searching)
        .focusedSceneValue(\.find, FindAction { searching = true })
        .sheet(isPresented: $renaming) {
            if let transcript {
                SpeakerNamesView(transcript: transcript) { names in rename(names) }
            }
        }
        .fileExporter(isPresented: Binding(get: { export != nil }, set: { if !$0 { export = nil } }),
                      document: export?.document, contentType: export?.type ?? .plainText,
                      defaultFilename: recording.title) { result in
            if case .failure(let error) = result {
                problem = ("The Transcript Cannot Be Exported", error.localizedDescription)
            }
        }
        .alert(problem?.title ?? "", isPresented: Binding(get: { problem != nil }, set: { if !$0 { problem = nil } })) {
        } message: {
            Text(problem?.message ?? "")
        }
        .task(id: recording.id) {
            query = ""
            load()
            player.load(library.store.audio(for: recording.id), recording: recording.id,
                        duration: Double(recording.durationMs ?? 0) / 1000)
        }
        .onDisappear { player.pause() }
    }

    private func segments(_ transcript: Transcript) -> some View {
        let current = currentSegment(in: transcript)
        let search = query.trimmingCharacters(in: .whitespaces)
        let shown = transcript.segments.enumerated().filter {
            search.isEmpty || $0.element.text.localizedStandardContains(search)
        }
        return ScrollViewReader { proxy in
            List(shown, id: \.offset) { index, segment in
                HStack(alignment: .firstTextBaseline, spacing: 12) {
                    Button(Timestamp.text(segment.startMs)) { player.play(fromMilliseconds: segment.startMs) }
                        .buttonStyle(.plain)
                        .font(.callout.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .help("Play from here")
                        .accessibilityLabel(Text("Play from \(Timestamp.text(segment.startMs))"))
                    if let speaker = segment.speaker {
                        Text(Speakers.name(speaker, in: transcript))
                            .font(.callout.weight(.semibold))
                            .foregroundStyle(.tint)
                    }
                    Text(Self.highlighting(search, in: segment.text))
                        .textSelection(.enabled)
                }
                .padding(.vertical, 2)
                .contextMenu {
                    Button("Copy") { Self.copy(segment.text) }
                    Button("Play from Here") { player.play(fromMilliseconds: segment.startMs) }
                }
                .listRowBackground(index == current ? Color.accentColor.opacity(0.12) : Color.clear)
                .id(index)
            }
            .listStyle(.plain)
            .overlay {
                if shown.isEmpty, !search.isEmpty {
                    ContentUnavailableView.search(text: search)
                }
            }
            .onChange(of: current) { _, index in
                guard player.isPlaying, let index else { return }
                withAnimation { proxy.scrollTo(index, anchor: .center) }
            }
        }
    }

    private func currentSegment(in transcript: Transcript) -> Int? {
        guard player.recordingID == recording.id, player.isPlaying || player.time > 0 else { return nil }
        let milliseconds = Int(player.time * 1000)
        return transcript.segments.lastIndex { $0.startMs <= milliseconds }
    }

    private func load() {
        do {
            let json = try library.store.transcriptJSON(for: recording.id)
            transcript = try Transcript(json: json)
            self.json = json
            failure = nil
        } catch {
            transcript = nil
            json = nil
            failure = error.localizedDescription
        }
    }

    private func rename(_ names: [Int: String]) {
        guard let json else { return }
        do {
            let renamed = try Transcript.renamingSpeakers(in: json, names: names)
            try library.store.saveTranscript(renamed, for: recording.id)
            load()
        } catch {
            problem = ("The Speaker Names Cannot Be Saved", error.localizedDescription)
        }
    }

    private static func highlighting(_ search: String, in text: String) -> AttributedString {
        var attributed = AttributedString(text)
        guard !search.isEmpty else { return attributed }
        var rest = attributed.startIndex..<attributed.endIndex
        while let found = attributed[rest].range(of: search, options: [.caseInsensitive, .diacriticInsensitive]) {
            attributed[found].backgroundColor = .yellow.opacity(0.4)
            rest = found.upperBound..<attributed.endIndex
        }
        return attributed
    }

    private static func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    private func copyText() {
        guard let json else { return }
        do {
            Self.copy(try Export.render(json, as: .txt, speakerLabel: String(localized: "Speaker {n}")))
        } catch {
            problem = ("The Transcript Cannot Be Copied", error.localizedDescription)
        }
    }

    private func prepareExport(_ format: ExportFormat) {
        guard let json else { return }
        do {
            let text = try Export.render(json, as: format, speakerLabel: String(localized: "Speaker {n}"))
            export = (TextDocument(text: text), format == .json ? .json : .plainText)
        } catch {
            problem = ("The Transcript Cannot Be Exported", error.localizedDescription)
        }
    }
}

struct FindAction {
    let focus: () -> Void

    func callAsFunction() {
        focus()
    }
}

extension FocusedValues {
    @Entry var find: FindAction?
}

private struct PlayerBar: View {
    @Environment(Player.self) private var player

    var body: some View {
        HStack(spacing: 12) {
            Button(player.isPlaying ? "Pause" : "Play",
                   systemImage: player.isPlaying ? "pause.fill" : "play.fill") { player.toggle() }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .font(.title3)
                .frame(width: 24)
                .keyboardShortcut(.space, modifiers: [])
            Text(Timestamp.text(Int(player.time * 1000)))
                .font(.callout.monospacedDigit())
                .foregroundStyle(.secondary)
            Slider(value: Binding(get: { min(player.time, player.duration) }, set: { player.seek(to: $0) }),
                   in: 0...max(player.duration, 0.1))
                .controlSize(.small)
            Text(Timestamp.text(Int(player.duration * 1000)))
                .font(.callout.monospacedDigit())
                .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
    }
}
