import Foundation
import SillokCore

public struct ModelInstaller: Sendable {
    public let store: ModelStore
    public let downloader: ModelDownloader

    public init(store: ModelStore, downloader: ModelDownloader = .shared) {
        self.store = store
        self.downloader = downloader
    }

    public func download(_ entry: ModelEntry, progress: ModelDownloader.Progress? = nil) async throws {
        let required = store.requiredSpace(for: entry)
        let available = store.availableDiskSpace()
        guard available >= required else { throw ModelError.insufficientDiskSpace(required: required, available: available) }

        let file = try await downloader.download(entry, into: store.downloadsDirectory, progress: progress)
        try store.install(verifiedFile: file, as: entry)
    }

    public func importFile(_ url: URL) throws -> ModelEntry {
        let hash = try FileHasher.sha256(of: url)
        guard let entry = store.manifest.entry(sha256: hash) else { throw ModelError.unknownModelFile }
        let staging = store.downloadsDirectory.appendingPathComponent("import-\(UUID().uuidString)-\(entry.fileName)")
        try FileManager.default.createDirectory(at: store.downloadsDirectory, withIntermediateDirectories: true)
        try FileManager.default.copyItem(at: url, to: staging)
        try store.install(verifiedFile: staging, as: entry)
        return entry
    }
}
