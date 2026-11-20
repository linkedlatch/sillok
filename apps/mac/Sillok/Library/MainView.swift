import SillokEngine
import SillokStore
import SwiftUI
import UniformTypeIdentifiers

struct MainView: View {
    enum Selection: Hashable {
        case job(UUID)
        case recording(UUID)
    }

    @Environment(ModelManager.self) private var models
    @Environment(Library.self) private var library
    @Environment(JobQueue.self) private var queue
    @State private var selection: Selection?
    @State private var importing = false
    @State private var pending: [URL] = []

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
                        RecordingRow(recording: recording).tag(Selection.recording(recording.id))
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 220, ideal: 260)
        } detail: {
            detail
        }
        .toolbar {
            Button("New Transcription", systemImage: "plus") { importing = true }
                .keyboardShortcut("n")
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio, .movie],
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
            NewJobView(sources: pending, languages: transcriptionModel?.languages ?? []) { options in
                let first = queue.items.count
                queue.enqueue(pending, options: options)
                if queue.items.indices.contains(first) { selection = .job(queue.items[first].id) }
            }
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
                JobView(item: item, aligns: transcriptionModel?.aligner == true)
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
                Button("New Transcription") { importing = true }
            }
        }
    }

    private var transcriptionModel: CatalogModel? {
        models.models.first { $0.role == .transcription }
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
