import Foundation
import SillokCore

public struct ModelInstaller: Sendable {
    public let store: ModelStore
    public let downloader: ResumableDownloader

    public init(store: ModelStore, downloader: ResumableDownloader = ResumableDownloader()) {
        self.store = store
        self.downloader = downloader
    }

    public func download(_ entry: ModelEntry, progress: ResumableDownloader.Progress? = nil) async throws {
        let part = store.partURL(for: entry)
        let alreadyReceived = (try? FileManager.default.attributesOfItem(atPath: part.path)[.size] as? NSNumber)?.int64Value ?? 0
        let required = store.requiredSpace(for: entry) - alreadyReceived
        let available = store.availableDiskSpace()
        guard available >= required else { throw ModelError.insufficientDiskSpace(required: required, available: available) }

        try await downloader.download(entry, partURL: part, progress: progress)
        try store.install(verifiedFile: part, as: entry)
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
