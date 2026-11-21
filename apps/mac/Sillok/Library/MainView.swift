import SillokEngine
import SillokStore
import SwiftUI
import UniformTypeIdentifiers

struct MainView: View {
    enum Selection: Hashable {
        case job(UUID)
        case recording(UUID)
    }

    @Environment(Library.self) private var library
    @Environment(JobQueue.self) private var queue
    @Environment(Player.self) private var player
    @Environment(Navigation.self) private var navigation
    @State private var selection: Selection?
    @State private var pending: [URL] = []
    @State private var deleting: Recording?

    var body: some View {
        NavigationSplitView {
            List(selection: $selection) {
                if !queue.items.isEmpty {
                    Section("Transcribing") {
                        ForEach(queue.items) { item in
                            JobRow(item: item).tag(Selection.job(item.id))
                        }
                    }
                }
                Section("Recordings") {
                    ForEach(library.recordings) { recording in
                        RecordingRow(recording: recording)
                            .tag(Selection.recording(recording.id))
                            .contextMenu {
                                Button("Delete…", role: .destructive) { deleting = recording }
                            }
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 220, ideal: 260)
            .onDeleteCommand {
                if case .recording(let id) = selection { deleting = library.recording(id) }
            }
        } detail: {
            detail
        }
        .toolbar {
            Button("New Transcription", systemImage: "plus") { navigation.importing = true }
        }
        .fileImporter(isPresented: Bindable(navigation).importing, allowedContentTypes: [.audio, .movie],
                      allowsMultipleSelection: true) { result in
            if case .success(let urls) = result, !urls.isEmpty { pending = urls }
        }
        .dropDestination(for: URL.self) { urls, _ in
            let files = urls.filter(\.isFileURL)
            guard !files.isEmpty else { return false }
            pending = files
            return true
        }
        .sheet(isPresented: Binding(get: { !pending.isEmpty }, set: { if !$0 { pending = [] } })) {
            NewJobView(sources: pending) { options, model in
                let first = queue.items.count
                queue.enqueue(pending, options: options, model: model)
                if queue.items.indices.contains(first) { selection = .job(queue.items[first].id) }
            }
        }
        .confirmationDialog(
            Text("Delete “\(deleting?.title ?? "")”?"),
            isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } }),
            presenting: deleting
        ) { recording in
            Button("Delete", role: .destructive) { delete(recording) }
        } message: { _ in
            Text("The recording and its transcript will be removed. This cannot be undone.")
        }
        .onChange(of: queue.completed?.item) {
            guard let completed = queue.completed, selection == .job(completed.item) else { return }
            selection = .recording(completed.recording)
        }
    }

    @ViewBuilder
    private var detail: some View {
        switch selection {
        case .job(let id):
            if let item = queue.item(id) {
                JobView(item: item)
            }
        case .recording(let id):
            if let recording = library.recording(id) {
                RecordingView(recording: recording)
            }
        case nil:
            ContentUnavailableView {
                Label("No Recording Selected", systemImage: "waveform")
            } description: {
                Text("Choose a recording, or drop audio and video files here to transcribe them.")
            } actions: {
                Button("New Transcription") { navigation.importing = true }
            }
        }
    }

    private func delete(_ recording: Recording) {
        player.unload(recording.id)
        if selection == .recording(recording.id) { selection = nil }
        library.delete(recording.id)
    }
}

private struct JobRow: View {
    let item: JobQueue.Item

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(item.title).lineLimit(1)
            status
                .font(.caption)
                .foregroundStyle(isFailed ? AnyShapeStyle(.red) : AnyShapeStyle(.secondary))
                .monospacedDigit()
        }
    }

    private var isFailed: Bool {
        if case .failed = item.state { true } else { false }
    }

    private var status: Text {
        switch item.state {
        case .waiting: Text("Waiting")
        case .reading: Text(Step.reading.title)
        case .running(let stage, let fraction) where fraction > 0:
            Text("\(Text(Step.stage(stage).title)) \(fraction.formatted(.percent.precision(.fractionLength(0))))")
        case .running(let stage, _):
            Text(Step.stage(stage).title)
        case .saving: Text(Step.saving.title)
        case .failed: Text("Failed")
        }
    }
}

private struct RecordingRow: View {
    let recording: Recording

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(recording.title).lineLimit(1)
            Text("\(recording.createdAt.formatted(date: .abbreviated, time: .shortened)) · \(Timestamp.text(recording.durationMs))")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}
