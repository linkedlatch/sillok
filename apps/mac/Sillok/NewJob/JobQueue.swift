import Foundation
import Observation
import SillokEngine
import SillokStore

@MainActor
@Observable
final class JobQueue {
    struct Item: Identifiable {
        let id: UUID
        let title: String
        let sourceName: String
        let options: JobOptions
        let modelID: String
        var source: URL?
        var state: State
        var previews = Previews()
        var step: Step?
        var stepStarted = Date.now
        var timings: [String: Int] = [:]

        var segments: [Transcript.Segment] { previews.segments }

        var isAdding: Bool {
            if case .adding = state { true } else { false }
        }

        var progress: (step: Step, fraction: Double)? {
            switch state {
            case .failed: nil
            case .adding(let fraction): (.adding, fraction)
            case .waiting: (.adding, 1)
            case .reading(let fraction): (.reading, fraction)
            case .running(let stage, let fraction): (.stage(stage), fraction)
            }
        }

        var remaining: Duration? {
            guard let progress, progress.step == step, progress.fraction >= 0.03 else { return nil }
            let elapsed = Date.now.timeIntervalSince(stepStarted)
            guard elapsed >= 20 else { return nil }
            return .seconds(elapsed * (1 - progress.fraction) / progress.fraction)
        }

        mutating func startStep(_ next: Step?, at time: Date) {
            guard next != step else { return }
            endStep(at: time)
            step = next
            stepStarted = time
        }

        mutating func endStep(at time: Date = .now) {
            if let step {
                timings[step.key, default: 0] += Int(time.timeIntervalSince(stepStarted) * 1000)
            }
            step = nil
        }
    }

    enum State: Equatable {
        case adding(Double)
        case waiting
        case reading(Double)
        case running(Stage, Double)
        case failed(String)
    }

    private(set) var items: [Item] = []
    private(set) var completed: UUID?
    private let models: ModelManager
    private let library: Library
    private var copier: (config: EngineConfig, engine: Engine)?
    private var transcriber: (config: EngineConfig, engine: Engine)?
    private var adder: Task<Void, Never>?
    private var worker: Task<Void, Never>?
    private var cancellers: [UUID: () -> Void] = [:]
    private var started = false
    private var activity: NSObjectProtocol?

    init(models: ModelManager, library: Library) {
        self.models = models
        self.library = library
    }

    var isAdding: Bool {
        items.contains(where: \.isAdding)
    }

    var isWorking: Bool {
        adder != nil || worker != nil
    }

    func uses(_ model: String) -> Bool {
        items.contains { $0.modelID == model }
    }

    func item(_ id: UUID) -> Item? {
        items.first { $0.id == id }
    }

    func position(of id: UUID) -> Int {
        (items.filter { $0.state == .waiting }.firstIndex { $0.id == id } ?? 0) + 1
    }

    func start() {
        guard !started else { return }
        started = true
        items = ((try? library.store.pending()) ?? []).map(Item.init) + items
        transcribeWaiting()
    }

    func enqueue(_ sources: [URL], options: JobOptions, model: CatalogModel) {
        items += sources.map { source in
            Item(id: UUID(), title: source.deletingPathExtension().lastPathComponent,
                 sourceName: source.lastPathComponent, options: options, modelID: model.id,
                 source: source, state: .adding(0))
        }
        Notifications.requestPermission()
        addPending()
    }

    func transcribeAgain(_ recording: Recording, options: JobOptions, model: CatalogModel) throws -> UUID {
        let copy = Recording(title: recording.title, sourceName: recording.sourceName, options: options,
                             transcriptionModel: model.id)
        try library.store.add(copyOf: recording.id, as: copy)
        items.append(Item(copy))
        Notifications.requestPermission()
        transcribeWaiting()
        return copy.id
    }

    func retry(_ id: UUID) {
        guard let item = item(id), case .failed = item.state else { return }
        if item.source != nil {
            update(id) { $0.state = .adding(0) }
            addPending()
        } else {
            try? library.store.update(id) { $0.error = nil }
            update(id) { $0.state = .waiting }
            transcribeWaiting()
        }
    }

    func cancel(_ id: UUID) {
        guard let item = item(id) else { return }
        items.removeAll { $0.id == id }
        if let cancel = cancellers[id] {
            cancel()
        } else if item.source == nil {
            try? library.store.delete(id)
        }
    }

    private func addPending() {
        guard adder == nil else { return }
        adder = Task {
            while let next = items.first(where: \.isAdding) {
                await add(next)
            }
            adder = nil
            keepAwake()
        }
        keepAwake()
    }

    private func transcribeWaiting() {
        guard worker == nil else { return }
        worker = Task {
            while let next = items.first(where: { $0.state == .waiting }) {
                await transcribe(next)
            }
            worker = nil
            keepAwake()
        }
        keepAwake()
    }

    private func keepAwake() {
        let busy = adder != nil || worker != nil
        if busy, activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(
                options: .userInitiated, reason: "Transcribing recordings")
        } else if !busy, let activity {
            ProcessInfo.processInfo.endActivity(activity)
            self.activity = nil
        }
    }

    private func add(_ item: Item) async {
        guard let source = item.source else { return }
        let accessing = source.startAccessingSecurityScopedResource()
        defer {
            if accessing { source.stopAccessingSecurityScopedResource() }
        }
        let store = library.store
        do {
            let config = EngineSettings.config(
                modelsDirectory: models.directory, transcriptionModel: models.transcriptionModel?.id ?? item.modelID)
            let engine = try cached(&copier, config)
            let staging = try store.makeStaging()
            do {
                try await perform(item.id) { events in
                    try await Transcription.copy(
                        engine: engine, input: source, output: staging.appending(path: RecordingStore.audioName),
                        events: events)
                }
                var recording = Recording(id: item.id, title: item.title, sourceName: item.sourceName,
                                          options: item.options, transcriptionModel: item.modelID)
                recording.timingsMs = self.item(item.id)?.timings
                try store.add(staging: staging, recording: recording)
            } catch {
                store.discard(staging: staging)
                throw error
            }
            guard self.item(item.id) != nil else {
                try? store.delete(item.id)
                return
            }
            update(item.id) {
                $0.source = nil
                $0.state = .waiting
            }
            transcribeWaiting()
        } catch is CancellationError {
        } catch {
            update(item.id) { $0.state = .failed(Self.describe(error)) }
        }
    }

    private func transcribe(_ item: Item) async {
        update(item.id) { $0.state = .reading(0) }
        let store = library.store
        do {
            let config = EngineSettings.config(modelsDirectory: models.directory, transcriptionModel: item.modelID)
            let engine = try cached(&transcriber, config)
            let result = try await perform(item.id) { events in
                try await Transcription.run(
                    engine: engine, copy: store.audio(for: item.id), options: item.options,
                    checkpoint: store.checkpoint(for: item.id), speech: store.speech(for: item.id),
                    events: events)
            }
            guard self.item(item.id) != nil else { throw CancellationError() }
            try store.finish(item.id, transcriptJSON: result.json, durationMs: result.transcript.durationMs,
                             timingsMs: self.item(item.id)?.timings ?? [:])
            library.reload()
            items.removeAll { $0.id == item.id }
            completed = item.id
            Notifications.finished(item.title)
        } catch _ where self.item(item.id) == nil {
            try? store.delete(item.id)
        } catch is CancellationError {
        } catch {
            let message = Self.describe(error)
            try? store.update(item.id) { $0.error = message }
            update(item.id) { $0.state = .failed(message) }
            Notifications.failed(item.title, message: message)
        }
    }

    private func cached(_ slot: inout (config: EngineConfig, engine: Engine)?, _ config: EngineConfig) throws -> Engine {
        if let slot, slot.config == config { return slot.engine }
        let engine = try Engine(config: config)
        slot = (config, engine)
        return engine
    }

    private func perform<T: Sendable>(
        _ id: UUID,
        _ body: @escaping @Sendable (@escaping @Sendable (TranscriptionEvent) -> Void) async throws -> T
    ) async throws -> T {
        let (events, continuation) = AsyncStream.makeStream(of: (event: TranscriptionEvent, time: Date).self)
        let task = Task.detached {
            defer { continuation.finish() }
            return try await body { continuation.yield(($0, .now)) }
        }
        cancellers[id] = { task.cancel() }
        defer {
            cancellers[id] = nil
            update(id) { $0.endStep() }
        }
        for await (event, time) in events {
            apply(event, at: time, to: id)
        }
        return try await task.value
    }

    private func apply(_ event: TranscriptionEvent, at time: Date, to id: UUID) {
        update(id) { item in
            switch event {
            case .reading(let fraction) where item.isAdding: item.state = .adding(fraction / 2)
            case .saving(let fraction): item.state = .adding(0.5 + fraction / 2)
            case .reading(let fraction): item.state = .reading(fraction)
            case .stage(let stage, let fraction): item.state = .running(stage, fraction)
            case .preview(let preview): item.previews.add(preview)
            }
            item.startStep(item.progress?.step, at: time)
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

extension JobQueue.Item {
    init(_ recording: Recording) {
        self.init(id: recording.id, title: recording.title, sourceName: recording.sourceName,
                  options: recording.options, modelID: recording.transcriptionModel ?? "",
                  state: recording.error.map(JobQueue.State.failed) ?? .waiting)
    }
}
