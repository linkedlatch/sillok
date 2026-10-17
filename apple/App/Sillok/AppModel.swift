import Foundation
import Observation
import SillokAudio
import SillokCore
import SillokModels
import SillokPlatform
import SillokNative

@MainActor
@Observable
final class AppModel {
    enum ModelStatus: Equatable {
        case missing
        case downloading(received: Int64, total: Int64)
        case installing
        case warmingUp
        case ready
        case failed(String)
    }

    let projectStore: ProjectStore
    let modelStore: ModelStore
    let installer: ModelInstaller
    let playback = PlaybackController()

    var projects: [TranscriptProject] = []
    var progress: [UUID: TranscribeProgress] = [:]
    var modelStatus = ModelStatus.missing
    var alert: String?
    var memoryWarning = false
    var settings = AppSettings.load() {
        didSet { settings.save() }
    }

    private(set) var runningJob: UUID?
    private(set) var playingProjectID: UUID?
    private let engine = NativeEngine()
    private let queue = JobQueue()
    private var loadedFiles: ModelFiles?
    private var modelTask: Task<Void, Never>?
    private var memoryMonitor: MemoryPressureMonitor?
    private var started = false
    private var deletionRequested: Set<UUID> = []

    init() {
        let support = (try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true))
            ?? FileManager.default.temporaryDirectory
        let root = support.appendingPathComponent("Sillok", isDirectory: true)
        let manifest = (try? ModelStore.bundledManifest()) ?? ModelManifest(version: 0, models: [])
        projectStore = ProjectStore(root: root.appendingPathComponent("Projects", isDirectory: true))
        modelStore = ModelStore(root: root.appendingPathComponent("Models", isDirectory: true), manifest: manifest)
        installer = ModelInstaller(store: modelStore)
    }

    func start() {
        guard !started else { return }
        started = true
        projects = projectStore.loadAll()
        memoryMonitor = MemoryPressureMonitor { [weak self] level in
            Task { @MainActor in
                guard let self, self.runningJob != nil else { return }
                self.memoryWarning = level == .critical
            }
        }
        refreshModelStatus()
    }

    var manifest: ModelManifest { modelStore.manifest }

    var whisperEntry: ModelEntry? { manifest.entry(settings.whisperID) }

    var whisperModels: [ModelEntry] { manifest.models.filter { $0.kind == .whisper } }

    var requiredModels: [ModelEntry] {
        ModelSelection.requiredEntries(whisperID: settings.whisperID, useCoreML: settings.useCoreML, diarization: settings.speakerDiarization, manifest: manifest)
    }

    var missingModels: [ModelEntry] {
        requiredModels.filter { !modelStore.isInstalled($0) }
    }

    var isBusy: Bool { runningJob != nil || modelTask != nil }

    func refreshModelStatus() {
        guard modelTask == nil else { return }
        let wasReady = modelStatus == .ready
        modelStatus = missingModels.isEmpty && !requiredModels.isEmpty ? .ready : .missing
        if modelStatus == .ready, !wasReady { enqueueUnfinished() }
    }

    func downloadModels() {
        guard !isBusy else { return }
        settings.hasChosenMode = true
        let entries = missingModels
        modelTask = Task {
            do {
                let total = entries.reduce(0) { $0 + $1.size }
                var completed: Int64 = 0
                for entry in entries {
                    let base = completed
                    modelStatus = .downloading(received: base, total: total)
                    try await installer.download(entry) { received, _ in
                        Task { @MainActor in
                            guard self.modelTask != nil, case .downloading = self.modelStatus else { return }
                            self.modelStatus = .downloading(received: base + received, total: total)
                        }
                    }
                    completed += entry.size
                }
                try await warmUp()
                finishModelTask(error: nil)
            } catch is CancellationError {
                finishModelTask(error: nil)
            } catch {
                finishModelTask(error: error)
            }
        }
    }

    func cancelDownload() {
        modelTask?.cancel()
    }

    func importModel(_ url: URL) {
        guard !isBusy else {
            alert = "전사나 다운로드가 끝난 뒤에 다시 시도해 주세요"
            return
        }
        modelStatus = .installing
        modelTask = Task {
            do {
                let installer = installer
                let entry = try await Task.detached {
                    try await SecurityScopedFile.withAccess(to: url) { try installer.importFile(url) }
                }.value
                if entry.kind == .coreMLEncoder { try await warmUp() }
                finishModelTask(error: nil)
            } catch {
                finishModelTask(error: error)
            }
        }
    }

    func removeModel(_ entry: ModelEntry) {
        guard !isBusy else {
            alert = "전사나 다운로드가 끝난 뒤에 다시 시도해 주세요"
            return
        }
        if loadedFiles != nil {
            loadedFiles = nil
            Task { await engine.unload() }
        }
        try? modelStore.remove(entry)
        refreshModelStatus()
    }

    private func finishModelTask(error: Error?) {
        modelTask = nil
        modelStatus = .missing
        refreshModelStatus()
        guard let error else { return }
        if modelStatus == .ready {
            alert = UserMessage.describe(error)
        } else {
            modelStatus = .failed(UserMessage.describe(error))
        }
    }

    private func warmUp() async throws {
        guard settings.useCoreML, let whisper = whisperEntry, let encoder = manifest.coreMLEncoder(for: whisper),
              modelStore.isInstalled(whisper), modelStore.isInstalled(encoder) else { return }
        modelStatus = .warmingUp
        try await prepareEngine()
        _ = try await engine.transcribe(samples: [Float](repeating: 0, count: PCMFormat.sampleRate), options: ChunkTranscribeOptions(language: "ko"))
        await engine.unload()
        loadedFiles = nil
    }

    private func prepareEngine() async throws {
        guard let whisper = whisperEntry else { throw ModelError.unknownModelFile }
        if engine.loadedFiles == nil { loadedFiles = nil }
        let encoder = manifest.coreMLEncoder(for: whisper).flatMap { modelStore.isInstalled($0) ? $0 : nil }
        if loadedFiles == modelStore.modelFiles(for: whisper, useCoreML: settings.useCoreML && encoder != nil) { return }
        #if os(iOS)
        let countEncoder = true
        #else
        let countEncoder = false
        #endif
        let fits = ModelSelection.shouldUseCoreML(whisper: whisper, encoder: encoder, availableMemory: MemoryProbe.availableBytes, countEncoder: countEncoder)
        let files = modelStore.modelFiles(for: whisper, useCoreML: settings.useCoreML && fits)
        if loadedFiles == files { return }
        loadedFiles = nil
        try await engine.load(files, mode: .foregroundAccelerated)
        loadedFiles = files
    }

    func addFiles(_ urls: [URL], fromDrop: Bool = false) {
        let unsupported = urls.filter { !FormatProbe.isSupported($0) }
        if !unsupported.isEmpty {
            alert = "지원하지 않는 파일 형식입니다: " + unsupported.map(\.lastPathComponent).joined(separator: ", ")
        }
        #if os(iOS)
        let copy = settings.copyOnImport || fromDrop
        #else
        let copy = settings.copyOnImport
        #endif
        for url in urls where FormatProbe.isSupported(url) {
            do {
                let id = UUID()
                let source: SourceReference
                if copy {
                    let name = "source." + url.pathExtension.lowercased()
                    let target = projectStore.directory(for: id).appendingPathComponent(name)
                    try FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                    let accessing = url.startAccessingSecurityScopedResource()
                    defer { if accessing { url.stopAccessingSecurityScopedResource() } }
                    try FileManager.default.copyItem(at: url, to: target)
                    source = .containerCopy(fileName: name)
                } else {
                    source = .bookmark(try SecurityScopedFile.bookmark(for: url))
                }
                let project = TranscriptProject(id: id, sourceName: url.lastPathComponent, source: source, options: settings.transcriptionOptions)
                try projectStore.save(project)
                projects.insert(project, at: 0)
                enqueue(id)
            } catch {
                alert = "\(url.lastPathComponent): \(UserMessage.describe(error))"
            }
        }
    }

    func project(_ id: UUID) -> TranscriptProject? {
        projects.first { $0.id == id }
    }

    func sourceURL(for project: TranscriptProject) -> URL? {
        switch project.source {
        case .containerCopy(let name):
            let url = projectStore.directory(for: project.id).appendingPathComponent(name)
            return FileManager.default.fileExists(atPath: url.path) ? url : nil
        case .bookmark(let data):
            switch SecurityScopedFile.resolve(data) {
            case .missing:
                return nil
            case .available(let url, let refreshed):
                if let refreshed { update(project.id) { $0.source = .bookmark(refreshed) } }
                return url
            }
        }
    }

    func relink(_ id: UUID, to url: URL) {
        do {
            let bookmark = try SecurityScopedFile.bookmark(for: url)
            update(id) {
                $0.source = .bookmark(bookmark)
                $0.sourceName = url.lastPathComponent
            }
        } catch {
            alert = UserMessage.describe(error)
        }
    }

    func play(_ project: TranscriptProject, at time: TimeInterval) {
        guard let url = sourceURL(for: project) else {
            alert = "원본 파일을 찾을 수 없습니다. 파일 위치를 다시 지정해 주세요"
            return
        }
        playback.play(url, at: time)
        playingProjectID = project.id
    }

    func retry(_ id: UUID) {
        update(id) { $0.status = .pending }
        enqueue(id)
    }

    func cancel(_ id: UUID) {
        Task {
            await queue.cancel(id)
            if runningJob != id { update(id) { $0.status = .cancelled } }
        }
    }

    func delete(_ id: UUID) {
        if playingProjectID == id {
            playback.stop()
            playingProjectID = nil
        }
        projects.removeAll { $0.id == id }
        progress[id] = nil
        if runningJob == id {
            deletionRequested.insert(id)
        } else {
            try? projectStore.delete(id)
        }
        Task { await queue.cancel(id) }
    }

    private func enqueueUnfinished() {
        for project in projects.reversed() where project.status == .pending || project.status == .transcribing {
            enqueue(project.id)
        }
    }

    private func enqueue(_ id: UUID) {
        Task {
            await queue.enqueue(id) { [weak self] in
                try await self?.run(id)
            }
        }
    }

    private func run(_ id: UUID) async throws {
        guard modelStatus == .ready, let whisper = whisperEntry, let vadEntry = manifest.entry(ModelSelection.vadID) else { return }
        guard let project = project(id), project.status != .completed else { return }
        guard let url = sourceURL(for: project) else {
            update(id) { $0.status = .failed("원본 파일을 찾을 수 없습니다. 파일 위치를 다시 지정해 주세요") }
            return
        }
        runningJob = id
        let accessing = url.startAccessingSecurityScopedResource()
        let activity = ActivityAssertion(reason: "전사")
        defer {
            if accessing { url.stopAccessingSecurityScopedResource() }
            withExtendedLifetime(activity) {}
            progress[id] = nil
            memoryWarning = false
            runningJob = nil
            if deletionRequested.remove(id) != nil { try? projectStore.delete(id) }
        }

        let diarizationModel = manifest.entry(ModelSelection.diarizationID).flatMap { modelStore.isInstalled($0) ? modelStore.location(of: $0) : nil }
        do {
            let source = try await AssetAudioSource(url: url)
            try await prepareEngine()
            update(id) {
                $0.status = .transcribing
                $0.duration = source.duration
                $0.modelID = whisper.id
                $0.segments = []
            }
            var pipeline = FileTranscriptionPipeline(
                core: NativeCore(engine: engine, vadModel: modelStore.location(of: vadEntry), diarizationModel: diarizationModel),
                options: project.options,
                modelID: whisper.id,
                checkpointURL: projectStore.checkpointURL(for: id)
            )
            pipeline.thermal = ThermalMonitor()
            for try await step in pipeline.run(source: source) {
                progress[id] = step
                if step.replacesSegments {
                    update(id) { $0.segments = step.newSegments }
                } else if !step.newSegments.isEmpty {
                    update(id, save: false) { $0.segments += step.newSegments }
                }
            }
            try Task.checkCancellation()
            update(id) { $0.status = .completed }
            try? FileManager.default.removeItem(at: projectStore.checkpointURL(for: id))
        } catch is CancellationError {
            update(id) { $0.status = .cancelled }
        } catch {
            update(id) { $0.status = .failed(UserMessage.describe(error)) }
        }

        if await queue.pendingIDs.isEmpty {
            await engine.unload()
            loadedFiles = nil
        }
    }

    private func update(_ id: UUID, save: Bool = true, _ change: (inout TranscriptProject) -> Void) {
        guard let index = projects.firstIndex(where: { $0.id == id }) else { return }
        change(&projects[index])
        projects[index].updatedAt = Date()
        if save { try? projectStore.save(projects[index]) }
    }
}
