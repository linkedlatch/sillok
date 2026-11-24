import Foundation
import Observation
import SillokEngine
import SillokStore

@MainActor
@Observable
final class ModelManager {
    enum State: Equatable {
        case checking
        case missing
        case ready
        case failed(String)
    }

    enum Check: Equatable {
        case running
        case intact
        case repaired(Int)
        case failed(String)
    }

    struct Download: Equatable {
        let key: String
        let started = Date.now
        var progress: InstallProgress?

        var bytesPerSecond: Int64? {
            guard let progress, progress.phase == .downloading else { return nil }
            let elapsed = Date.now.timeIntervalSince(started)
            return elapsed >= 1 ? Int64(Double(progress.completedBytes) / elapsed) : nil
        }
    }

    private(set) var state: State = .checking
    private(set) var models: [CatalogModel] = []
    private(set) var transcriptionModels: [CatalogModel] = []
    private(set) var installed: Set<String> = []
    private(set) var downloadSizes: [String: Int64] = [:]
    private(set) var installedEncoders: Set<String> = []
    private(set) var partlyDownloaded: Set<String> = []
    private(set) var download: Download?
    private(set) var failure: (key: String, message: String)?
    private(set) var check: Check?
    let directory: URL
    private var installTask: Task<Void, Never>?

    init(directory: URL = .applicationSupportDirectory.appending(path: "Models")) {
        self.directory = directory
    }

    var transcriptionModel: CatalogModel? {
        models.first { $0.role == .transcription }
    }

    var transcriptionFamilies: [[CatalogModel]] {
        var families: [[CatalogModel]] = []
        for model in transcriptionModels {
            if let index = families.firstIndex(where: { $0[0].family == model.family }) {
                families[index].append(model)
            } else {
                families.append([model])
            }
        }
        return families
    }

    var downloadSize: Int64 {
        transcriptionModel.flatMap { downloadSizes[$0.id] } ?? 0
    }

    func isInstalled(_ model: CatalogModel) -> Bool {
        installed.contains(model.id)
    }

    func encoder(of model: CatalogModel) -> CatalogFile? {
        model.files.first(where: \.zip)
    }

    func isInstalled(_ encoder: CatalogFile) -> Bool {
        installedEncoders.contains(encoder.file)
    }

    func memory(of model: CatalogModel) -> Int64 {
        let memory = model.memory ?? 0
        guard EngineSettings.npu, let encoder = encoder(of: model), isInstalled(encoder) || !isInstalled(model)
        else { return memory }
        return memory + encoder.size
    }

    func encoderDownload(for model: CatalogModel) -> CatalogFile? {
        guard EngineSettings.npu, let encoder = encoder(of: model), !isInstalled(encoder) else { return nil }
        return encoder
    }

    func downloadSize(of encoder: CatalogFile) -> Int64 {
        downloadSizes[encoder.file] ?? 0
    }

    func refresh() {
        do {
            transcriptionModels = try Catalog.models().filter { $0.role == .transcription }
            if let id = EngineSettings.transcriptionModel, !transcriptionModels.contains(where: { $0.id == id }) {
                UserDefaults.standard.removeObject(forKey: EngineSettings.transcriptionModelKey)
            }
            var installed: Set<String> = []
            var sizes: [String: Int64] = [:]
            var encoders: Set<String> = []
            var partial: Set<String> = []
            for model in transcriptionModels {
                let pending = try pendingFiles(for: model.id)
                if pending.allSatisfy(\.file.zip) {
                    installed.insert(model.id)
                } else {
                    sizes[model.id] = pending.filter { !$0.file.zip }.map(\.remainingBytes).reduce(0, +)
                    let others = transcriptionModels.filter { $0.id != model.id }
                    if ModelInstaller.isStarted(model, sharingFilesWith: others, pending: pending) {
                        partial.insert(model.id)
                    }
                }
                for file in model.files where file.zip {
                    if let item = pending.first(where: { $0.file == file }) {
                        sizes[file.file] = item.remainingBytes
                        if item.downloadedBytes > 0 { partial.insert(file.file) }
                    } else {
                        encoders.insert(file.file)
                    }
                }
            }
            self.installed = installed
            downloadSizes = sizes
            installedEncoders = encoders
            partlyDownloaded = partial
            models = try Catalog.engineModels(transcriptionModel: EngineSettings.transcriptionModel)
            state = transcriptionModel.map(isInstalled) == true ? .ready : .missing
        } catch {
            state = .failed(Self.describe(error))
        }
    }

    func choose(transcriptionModel id: String) {
        if transcriptionModels.first(where: { $0.id == id })?.recommended == true {
            UserDefaults.standard.removeObject(forKey: EngineSettings.transcriptionModelKey)
        } else {
            UserDefaults.standard.set(id, forKey: EngineSettings.transcriptionModelKey)
        }
        refresh()
    }

    func remove(_ id: String) {
        Task {
            await stopDownload(key: id)
            guard download == nil, let model = transcriptionModels.first(where: { $0.id == id }),
                id != transcriptionModel?.id || !isInstalled(model)
            else { return }
            let others = transcriptionModels.filter {
                $0.id != id && (isInstalled($0) || partlyDownloaded.contains($0.id))
            }
            remove(key: id) {
                try ModelInstaller.remove(model, keepingFilesOf: others, from: directory)
            }
        }
    }

    func remove(_ encoder: CatalogFile) {
        Task {
            await stopDownload(key: encoder.file)
            guard download == nil else { return }
            remove(key: encoder.file) {
                try ModelInstaller.remove([encoder], from: directory)
            }
        }
    }

    private func stopDownload(key: String) async {
        guard download?.key == key else { return }
        installTask?.cancel()
        await installTask?.value
    }

    private func remove(key: String, files: () throws -> Void) {
        if failure?.key == key { failure = nil }
        do {
            try files()
        } catch {
            failure = (key, Self.describe(error))
        }
        refresh()
    }

    func install(_ id: String) {
        start(key: id) { [self] in
            Self.wanted(try pendingFiles(for: id))
        } done: { [self] in
            choose(transcriptionModel: id)
        }
    }

    func install(_ encoder: CatalogFile) {
        guard let model = transcriptionModels.first(where: { $0.files.contains(encoder) }) else { return }
        start(key: encoder.file) { [self] in
            try pendingFiles(for: model.id).filter { $0.file == encoder }
        } done: { [self] in
            refresh()
        }
    }

    func pause() {
        installTask?.cancel()
    }

    func checkForDamage() {
        guard download == nil, check != .running else { return }
        check = .running
        let directory = directory
        Task {
            do {
                let damaged = try await Task.detached {
                    try Catalog.check(directory: directory, verifyHash: true)
                        .filter { $0.status == .wrongHash || $0.status == .wrongSize }
                }.value
                let files = Set(damaged.map(\.file))
                for file in files {
                    try FileManager.default.removeItem(at: directory.appending(path: file))
                }
                check = files.isEmpty ? .intact : .repaired(files.count)
            } catch {
                check = .failed(Self.describe(error))
            }
            refresh()
        }
    }

    private func pendingFiles(for id: String) throws -> [PendingFile] {
        try ModelInstaller.pendingFiles(for: Catalog.engineModels(transcriptionModel: id), in: directory)
    }

    private static func wanted(_ pending: [PendingFile]) -> [PendingFile] {
        EngineSettings.npu ? pending : pending.filter { !$0.file.zip }
    }

    private func start(key: String, files: @escaping () throws -> [PendingFile], done: @escaping () -> Void) {
        guard installTask == nil else { return }
        download = Download(key: key)
        failure = nil
        installTask = Task {
            defer {
                installTask = nil
                download = nil
            }
            do {
                try await ModelInstaller.install(files(), into: directory) { progress in
                    Task { @MainActor in self.update(progress) }
                }
                done()
            } catch {
                if !Task.isCancelled {
                    failure = (key, Self.describe(error))
                }
                refresh()
            }
        }
    }

    private func update(_ progress: InstallProgress) {
        guard download != nil else { return }
        download?.progress = progress
    }

    static func describe(_ error: Error) -> String {
        switch error {
        case ModelInstallError.wrongSize(let file):
            String(localized: "\(file) did not download completely.")
        case ModelInstallError.wrongHash(let file):
            String(localized: "\(file) is damaged.")
        case ModelInstallError.badResponse(let file, let status):
            String(localized: "The server refused \(file) (HTTP \(status)).")
        case ModelInstallError.extractionFailed(let file):
            String(localized: "\(file) could not be extracted.")
        default:
            error.localizedDescription
        }
    }
}
