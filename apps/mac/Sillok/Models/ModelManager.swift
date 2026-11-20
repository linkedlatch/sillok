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
        case installing(InstallProgress?)
        case ready
        case failed(String)
    }

    private(set) var state: State = .checking
    private(set) var models: [CatalogModel] = []
    private(set) var downloadSize: Int64 = 0
    let directory: URL
    private var installTask: Task<Void, Never>?

    init(directory: URL = .applicationSupportDirectory.appending(path: "Models")) {
        self.directory = directory
    }

    func refresh() {
        do {
            models = try Catalog.engineModels()
            let pending = try ModelInstaller.pendingFiles(for: models, in: directory)
            downloadSize = pending.filter(\.needsDownload).map(\.file.size).reduce(0, +)
            state = pending.isEmpty ? .ready : .missing
        } catch {
            state = .failed(Self.describe(error))
        }
    }

    func install() {
        guard installTask == nil else { return }
        state = .installing(nil)
        installTask = Task {
            defer { installTask = nil }
            do {
                let pending = try ModelInstaller.pendingFiles(for: models, in: directory)
                try await ModelInstaller.install(pending, into: directory) { progress in
                    Task { @MainActor in self.update(progress) }
                }
                refresh()
            } catch {
                if Task.isCancelled {
                    refresh()
                } else {
                    state = .failed(Self.describe(error))
                }
            }
        }
    }

    func cancel() {
        installTask?.cancel()
    }

    private func update(_ progress: InstallProgress) {
        guard case .installing = state else { return }
        state = .installing(progress)
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
