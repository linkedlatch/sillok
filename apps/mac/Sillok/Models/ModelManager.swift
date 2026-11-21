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

    struct Download: Equatable {
        let model: String
        var progress: InstallProgress?
    }

    private(set) var state: State = .checking
    private(set) var models: [CatalogModel] = []
    private(set) var transcriptionModels: [CatalogModel] = []
    private(set) var downloadSizes: [String: Int64] = [:]
    private(set) var download: Download?
    private(set) var failure: (model: String, message: String)?
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
        downloadSizes[model.id] == nil
    }

    func refresh() {
        do {
            transcriptionModels = try Catalog.models().filter { $0.role == .transcription }
            if let id = EngineSettings.transcriptionModel, !transcriptionModels.contains(where: { $0.id == id }) {
                UserDefaults.standard.removeObject(forKey: EngineSettings.transcriptionModelKey)
            }
            var sizes: [String: Int64] = [:]
            for model in transcriptionModels {
                let pending = try ModelInstaller.pendingFiles(
                    for: Catalog.engineModels(transcriptionModel: model.id), in: directory)
                if !pending.isEmpty {
                    sizes[model.id] = pending.filter(\.needsDownload).map(\.file.size).reduce(0, +)
                }
            }
            downloadSizes = sizes
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

    func install(_ id: String) {
        guard installTask == nil else { return }
        download = Download(model: id)
        failure = nil
        installTask = Task {
            defer {
                installTask = nil
                download = nil
            }
            do {
                let models = try Catalog.engineModels(transcriptionModel: id)
                let pending = try ModelInstaller.pendingFiles(for: models, in: directory)
                try await ModelInstaller.install(pending, into: directory) { progress in
                    Task { @MainActor in self.update(progress) }
                }
                choose(transcriptionModel: id)
            } catch {
                if !Task.isCancelled {
                    failure = (id, Self.describe(error))
                }
                refresh()
            }
        }
    }

    func cancel() {
        installTask?.cancel()
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
