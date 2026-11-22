import SillokEngine
import SillokStore
import SwiftUI
import UniformTypeIdentifiers

struct RecordingView: View {
    let recording: Recording
    let initialQuery: String
    @Environment(Library.self) private var library
    @Environment(Player.self) private var player
    @State private var document: TranscriptDocument?
    @State private var failure: String?
    @State private var renaming = false
    @State private var export: (document: TextDocument, type: UTType)?
    @State private var problem: (title: LocalizedStringKey, message: String)?
    @State private var query = ""
    @State private var showingInfo = false
    @FocusState private var searching: Bool
    @State private var following = true
    @State private var editing: Int?
    @State private var draft = ""
    @FocusState private var editorFocused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 4) {
                Text(recording.title)
                    .font(.title2.bold())
                Text("\(recording.date.formatted(date: .abbreviated, time: .shortened)) · \(Timestamp.text(recording.durationMs ?? 0))")
                    .foregroundStyle(.secondary)
            }
            .padding(24)

            if let document {
                segments(document)
                Divider()
                PlayerBar()
            } else if let failure {
                ContentUnavailableView("The Transcript Cannot Be Read", systemImage: "exclamationmark.triangle",
                                       description: Text(failure))
            }
        }
        .toolbar {
            if let document, !document.transcript.speakers.isEmpty {
                Button("Speaker Names", systemImage: "person.2") { renaming = true }
            }
            Button("Copy Text", systemImage: "doc.on.doc") { copyText() }
                .disabled(document == nil)
            Menu("Export", systemImage: "square.and.arrow.up") {
                Button("Text") { prepareExport(.txt) }
                Button("JSON") { prepareExport(.json) }
            }
            .disabled(document == nil)
            Button("Info", systemImage: "info.circle") { showingInfo = true }
                .popover(isPresented: $showingInfo) {
                    RecordingInfoView(recording: recording, transcript: document?.transcript)
                }
        }
        .searchable(text: $query, placement: .toolbar, prompt: Text("Search Transcript"))
        .searchFocused($searching)
        .focusedSceneValue(\.find, FindAction { searching = true })
        .sheet(isPresented: $renaming) {
            if let document {
                SpeakerNamesView(transcript: document.transcript) { names in
                    save { try document.renameSpeakers(names) }
                }
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
            query = initialQuery
            load()
            player.load(library.store.audio(for: recording.id), recording: recording.id,
                        duration: Double(recording.durationMs ?? 0) / 1000)
        }
        .onDisappear { player.pause() }
    }

    private func segments(_ document: TranscriptDocument) -> some View {
        let transcript = document.transcript
        let current = currentSegment(in: transcript)
        let search = query.trimmingCharacters(in: .whitespaces)
        let shown = transcript.segments.enumerated().filter {
            search.isEmpty || $0.element.text.localizedStandardContains(search)
        }
        return ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    ForEach(shown, id: \.offset) { index, segment in
                        row(index, segment, in: document, search: search)
                            .padding(.horizontal, 16)
                            .padding(.vertical, 6)
                            .background(index == current ? Color.accentColor.opacity(0.12) : Color.clear)
                            .contextMenu { menu(index, segment, in: document) }
                            .id(index)
                        Divider()
                            .padding(.leading, 16)
                    }
                }
            }
            .onScrollPhaseChange { _, phase in
                if phase == .interacting { following = false }
            }
            .overlay {
                if transcript.segments.isEmpty {
                    ContentUnavailableView("No Speech Found", systemImage: "waveform.slash",
                                           description: Text("Sillok heard no speech in this recording."))
                } else if shown.isEmpty, !search.isEmpty {
                    ContentUnavailableView.search(text: search)
                }
            }
            .overlay(alignment: .bottom) {
                if player.isPlaying, !following, search.isEmpty, let current {
                    Button("Now Playing", systemImage: "waveform") {
                        following = true
                        withAnimation { proxy.scrollTo(current, anchor: .center) }
                    }
                    .buttonStyle(.borderedProminent)
                    .buttonBorderShape(.capsule)
                    .padding(.bottom, 12)
                }
            }
            .onChange(of: current) { _, index in
                guard player.isPlaying, following, let index else { return }
                withAnimation { proxy.scrollTo(index, anchor: .center) }
            }
            .onChange(of: editorFocused) { _, focused in
                if !focused, let editing { commit(editing, in: document) }
            }
        }
    }

    private func row(_ index: Int, _ segment: Transcript.Segment, in document: TranscriptDocument,
                     search: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Button(Timestamp.text(segment.startMs)) { play(from: segment.startMs) }
                .buttonStyle(.plain)
                .font(.callout.monospacedDigit())
                .foregroundStyle(.secondary)
                .help("Play from here")
                .accessibilityLabel(Text("Play from \(Timestamp.text(segment.startMs))"))
            if let speaker = segment.speaker {
                Text(Speakers.name(speaker, in: document.transcript))
                    .font(.callout.weight(.semibold))
                    .foregroundStyle(.tint)
            }
            if editing == index {
                TextField("Text", text: $draft, axis: .vertical)
                    .textFieldStyle(.plain)
                    .focused($editorFocused)
                    .onAppear { editorFocused = true }
                    .onSubmit { commit(index, in: document) }
                    .onExitCommand { editing = nil }
            } else {
                Text(Self.highlighting(search, in: segment.text))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { edit(index, segment) }
                    .simultaneousGesture(TapGesture().onEnded { player.seek(to: Double(segment.startMs) / 1000) })
            }
            if document.edits.isEdited(index) {
                Image(systemName: "pencil")
                    .foregroundStyle(.secondary)
                    .help(Text("Edited. The original: \(document.originalText(of: index))"))
            }
        }
    }

    @ViewBuilder
    private func menu(_ index: Int, _ segment: Transcript.Segment, in document: TranscriptDocument) -> some View {
        Button("Copy") { Self.copy(segment.text) }
        Button("Play from Here") { play(from: segment.startMs) }
        Divider()
        Button("Edit Text") { edit(index, segment) }
        if !document.transcript.speakers.isEmpty {
            Picker("Speaker", selection: Binding(
                get: { segment.speaker },
                set: { speaker in if let speaker { save { try document.setSpeaker(speaker, of: index) } } }
            )) {
                ForEach(document.transcript.speakers) { speaker in
                    Text(Speakers.name(speaker.id, in: document.transcript)).tag(Int?.some(speaker.id))
                }
            }
        }
        if document.edits.isEdited(index) {
            Button("Restore the Original") { save { try document.restore(index) } }
        }
    }

    private func edit(_ index: Int, _ segment: Transcript.Segment) {
        draft = segment.text
        editing = index
    }

    private func commit(_ index: Int, in document: TranscriptDocument) {
        editing = nil
        save { try document.setText(draft, of: index) }
    }

    private func play(from milliseconds: Int) {
        following = true
        player.play(fromMilliseconds: milliseconds)
    }

    private func currentSegment(in transcript: Transcript) -> Int? {
        guard player.recordingID == recording.id, player.isPlaying || player.time > 0 else { return nil }
        let milliseconds = Int(player.time * 1000)
        return transcript.segments.lastIndex { $0.startMs <= milliseconds }
    }

    private func load() {
        do {
            document = try TranscriptDocument(id: recording.id, store: library.store)
            failure = nil
        } catch {
            document = nil
            failure = error.localizedDescription
        }
    }

    private func save(_ change: () throws -> Void) {
        do {
            try change()
        } catch {
            problem = ("The Change Cannot Be Saved", error.localizedDescription)
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

    private static func render(_ transcript: Transcript, as format: ExportFormat) throws -> String {
        try Export.render(transcript.json(), as: format, speakerLabel: String(localized: "Speaker {n}"))
    }

    private func copyText() {
        guard let document else { return }
        do {
            Self.copy(try Self.render(document.transcript, as: .txt))
        } catch {
            problem = ("The Transcript Cannot Be Copied", error.localizedDescription)
        }
    }

    private func prepareExport(_ format: ExportFormat) {
        guard let document else { return }
        do {
            let text = try Self.render(document.transcript, as: format)
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
            Button("Back 5 Seconds", systemImage: "gobackward.5") { player.skip(by: -5) }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .help("Back 5 seconds (←)")
            Button("Forward 5 Seconds", systemImage: "goforward.5") { player.skip(by: 5) }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .help("Forward 5 seconds (→)")
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
