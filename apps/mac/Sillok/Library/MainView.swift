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
    @State private var selection: Set<Selection> = []
    @State private var deleting: Set<Selection> = []
    @State private var renaming: Recording?
    @State private var transcribingAgain: Recording?
    @State private var problem: String?
    @State private var title = ""
    @State private var query = ""
    @State private var matches: Set<UUID>?

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
                    ForEach(shownRecordings) { recording in
                        RecordingRow(recording: recording)
                            .tag(Selection.recording(recording.id))
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 220, ideal: 260)
            .searchable(text: $query, placement: .sidebar, prompt: Text("Search Recordings"))
            .overlay {
                if matches?.isEmpty == true { ContentUnavailableView.search(text: query) }
            }
            .task(id: query) { await search() }
            .contextMenu(forSelectionType: Selection.self) { items in
                if items.count == 1, case .recording(let id) = items.first, let recording = library.recording(id) {
                    Button("Rename…") {
                        title = recording.title
                        renaming = recording
                    }
                    Button("Transcribe Again…") { transcribingAgain = recording }
                }
                Button("Delete…", role: .destructive) { deleting = items }
            }
            .onDeleteCommand { deleting = selection }
        } detail: {
            detail
        }
        .toolbar {
            Button("New Transcription", systemImage: "plus") { navigation.importing = true }
        }
        .fileImporter(isPresented: Bindable(navigation).importing, allowedContentTypes: [.audio, .movie],
                      allowsMultipleSelection: true) { result in
            if case .success(let urls) = result { navigation.adding = urls }
        }
        .dropDestination(for: URL.self) { urls, _ in
            navigation.adding = urls.filter(\.isFileURL)
            return !navigation.adding.isEmpty
        }
        .sheet(isPresented: Binding(get: { !navigation.adding.isEmpty }, set: { if !$0 { navigation.adding = [] } })) {
            NewJobView(files: navigation.adding.map(\.lastPathComponent)) { options, model in
                let first = queue.items.count
                queue.enqueue(navigation.adding, options: options, model: model)
                if queue.items.indices.contains(first) { selection = [.job(queue.items[first].id)] }
            }
        }
        .sheet(item: $transcribingAgain) { recording in
            NewJobView(files: [recording.title]) { options, model in
                do {
                    selection = [.job(try queue.transcribeAgain(recording, options: options, model: model))]
                } catch {
                    problem = error.localizedDescription
                }
            }
        }
        .alert("The Recording Cannot Be Transcribed Again",
               isPresented: Binding(get: { problem != nil }, set: { if !$0 { problem = nil } })) {
        } message: {
            Text(problem ?? "")
        }
        .confirmationDialog(
            deletingTitle,
            isPresented: Binding(get: { !deleting.isEmpty }, set: { if !$0 { deleting = [] } }),
            presenting: deleting
        ) { items in
            Button("Delete", role: .destructive) { delete(items) }
        } message: { _ in
            Text("Recordings and their transcripts are removed, and files still transcribing stop. This cannot be undone.")
        }
        .alert("Rename Recording",
               isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } }),
               presenting: renaming) { recording in
            TextField("Title", text: $title)
            Button("Rename") { library.rename(recording.id, to: title) }
            Button("Cancel", role: .cancel) {}
        }
        .onChange(of: queue.items.map(\.id)) {
            selection = selection.filter {
                guard case .job(let id) = $0 else { return true }
                return queue.item(id) != nil || queue.completed == id
            }
        }
        .onChange(of: queue.completed) {
            guard let id = queue.completed, selection.remove(.job(id)) != nil else { return }
            selection.insert(.recording(id))
        }
        .onKeyPress(keys: [.leftArrow, .rightArrow]) { press in
            guard selection.count == 1, case .recording = selection.first else { return .ignored }
            player.skip(by: press.key == .leftArrow ? -5 : 5)
            return .handled
        }
        .task { queue.start() }
    }

    @ViewBuilder
    private var detail: some View {
        if selection.count > 1 {
            ContentUnavailableView {
                Label("\(selection.count) Items Selected", systemImage: "square.stack")
            } actions: {
                Button("Delete…") { deleting = selection }
            }
        } else {
            switch selection.first {
            case .job(let id):
                if let item = queue.item(id) {
                    JobView(item: item)
                        .id(id)
                }
            case .recording(let id):
                if let recording = library.recording(id) {
                    RecordingView(recording: recording, initialQuery: matches == nil ? "" : query)
                        .id(id)
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
    }

    private var shownRecordings: [Recording] {
        guard let matches else { return library.recordings }
        return library.recordings.filter { matches.contains($0.id) }
    }

    private func search() async {
        let query = query.trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty else {
            matches = nil
            return
        }
        do {
            try await Task.sleep(for: .milliseconds(200))
            let found = try await library.search(query)
            try Task.checkCancellation()
            matches = found
        } catch {
            return
        }
    }

    private var deletingTitle: Text {
        guard deleting.count == 1, let item = deleting.first else { return Text("Delete \(deleting.count) Items?") }
        return Text("Delete “\(title(of: item))”?")
    }

    private func title(of item: Selection) -> String {
        switch item {
        case .job(let id): queue.item(id)?.title ?? ""
        case .recording(let id): library.recording(id)?.title ?? ""
        }
    }

    private func delete(_ items: Set<Selection>) {
        selection.subtract(items)
        for item in items {
            switch item {
            case .job(let id):
                queue.cancel(id)
            case .recording(let id):
                player.unload(id)
                library.delete(id)
            }
        }
    }
}

private struct JobRow: View {
    let item: JobQueue.Item
    @Environment(JobQueue.self) private var queue

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
        case .adding(let fraction) where fraction > 0:
            Text("\(Text(Step.adding.title)) \(fraction.formatted(.percent.precision(.fractionLength(0))))")
        case .adding: Text(Step.adding.title)
        case .waiting: Text("Waiting (\(queue.position(of: item.id)))")
        case .reading: Text(Step.reading.title)
        case .running(let stage, let fraction) where fraction > 0:
            Text("\(Text(Step.stage(stage).title)) \(fraction.formatted(.percent.precision(.fractionLength(0))))")
        case .running(let stage, _):
            Text(Step.stage(stage).title)
        case .failed: Text("Failed")
        }
    }
}

private struct RecordingRow: View {
    let recording: Recording

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(recording.title).lineLimit(1)
            Text("\(recording.date.formatted(date: .abbreviated, time: .shortened)) · \(Timestamp.text(recording.durationMs ?? 0))")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}
