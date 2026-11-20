import Foundation
import Observation
import SillokEngine
import SillokStore

@MainActor
@Observable
final class JobQueue {
    struct Item: Identifiable {
        let id = UUID()
        let source: URL
        let options: JobOptions
        var state: State = .waiting
        var segments: [Transcript.Segment] = []

        var title: String { source.deletingPathExtension().lastPathComponent }
    }

    enum State: Equatable {
        case waiting
        case reading(Double)
        case running(Stage, Double)
        case saving(Double)
        case failed(String)
    }

    private(set) var items: [Item] = []
    private(set) var completed: (item: UUID, recording: UUID)?
    private let models: ModelManager
    private let library: Library
    private var engine: Engine?
    private var worker: Task<Void, Never>?
    private var running: (id: UUID, task: Task<TranscriptionResult, Error>)?

    init(models: ModelManager, library: Library) {
        self.models = models
        self.library = library
    }

    func item(_ id: UUID) -> Item? {
        items.first { $0.id == id }
    }

    func enqueue(_ sources: [URL], options: JobOptions) {
        items += sources.map { Item(source: $0, options: options) }
        guard worker == nil else { return }
        worker = Task {
            while let next = items.first(where: { $0.state == .waiting }) {
                await process(next)
            }
            worker = nil
        }
    }

    func cancel(_ id: UUID) {
        if running?.id == id {
            running?.task.cancel()
        } else {
            remove(id)
        }
    }

    func remove(_ id: UUID) {
        items.removeAll { $0.id == id }
    }

    private func process(_ item: Item) async {
        let accessing = item.source.startAccessingSecurityScopedResource()
        defer {
            if accessing { item.source.stopAccessingSecurityScopedResource() }
        }
        update(item.id) { $0.state = .reading(0) }
        do {
            let engine = try engine ?? Engine(config: EngineConfig(modelsDirectory: models.directory))
            self.engine = engine
            let staging = try library.store.makeStaging()
            do {
                let result = try await transcribe(item, engine: engine, into: staging)
                let recording = Recording(title: item.title, sourceName: item.source.lastPathComponent,
                                          durationMs: result.transcript.durationMs, options: item.options)
                try library.store.commit(staging: staging, recording: recording, transcriptJSON: result.json)
                library.reload()
                remove(item.id)
                completed = (item.id, recording.id)
            } catch {
                library.store.discard(staging: staging)
                throw error
            }
        } catch is CancellationError {
            remove(item.id)
        } catch {
            update(item.id) { $0.state = .failed(Self.describe(error)) }
        }
    }

    private func transcribe(_ item: Item, engine: Engine, into staging: URL) async throws -> TranscriptionResult {
        let (events, continuation) = AsyncStream.makeStream(of: TranscriptionEvent.self)
        let task = Task.detached {
            defer { continuation.finish() }
            return try await Transcription.run(
                engine: engine, input: item.source, options: item.options,
                audioOutput: staging.appending(path: RecordingStore.audioName)
            ) { continuation.yield($0) }
        }
        running = (item.id, task)
        defer { running = nil }
        for await event in events {
            apply(event, to: item.id)
        }
        return try await task.value
    }

    private func apply(_ event: TranscriptionEvent, to id: UUID) {
        update(id) { item in
            switch event {
            case .reading(let fraction): item.state = .reading(fraction)
            case .stage(let stage, let fraction): item.state = .running(stage, fraction)
            case .saving(let fraction): item.state = .saving(fraction)
            case .segments(let segments): item.segments += segments
            }
        }
    }

    private func update(_ id: UUID, _ change: (inout Item) -> Void) {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return }
        change(&items[index])
    }

    static func describe(_ error: Error) -> String {
        if let error = error as? SillokError {
            switch error.code {
            case .mediaError: return String(localized: "Sillok cannot read the audio of this file.")
            case .modelError: return String(localized: "A model failed to load: \(error.message)")
            default: return error.message
            }
        }
        return error.localizedDescription
    }
}
