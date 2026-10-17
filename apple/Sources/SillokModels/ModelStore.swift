import Foundation
import SillokCore

public struct ModelStore: Sendable {
    public let root: URL
    public let manifest: ModelManifest

    public init(root: URL, manifest: ModelManifest) {
        self.root = root
        self.manifest = manifest
    }

    public static func bundledManifest(in bundle: Bundle = .main) throws -> ModelManifest {
        guard let url = bundle.url(forResource: "manifest", withExtension: "json") else {
            throw CocoaError(.fileNoSuchFile)
        }
        return try JSONDecoder().decode(ModelManifest.self, from: Data(contentsOf: url))
    }

    public var downloadsDirectory: URL { root.appendingPathComponent(".downloads", isDirectory: true) }

    public func location(of entry: ModelEntry) -> URL {
        root.appendingPathComponent(entry.installedName)
    }

    public func isInstalled(_ entry: ModelEntry) -> Bool {
        let url = location(of: entry)
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory) else { return false }
        if entry.kind == .coreMLEncoder { return isDirectory.boolValue }
        let size = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.int64Value
        return size == entry.size
    }

    public func modelFiles(for whisper: ModelEntry, useCoreML: Bool) -> ModelFiles {
        let encoder = useCoreML ? manifest.coreMLEncoder(for: whisper).flatMap { isInstalled($0) ? location(of: $0) : nil } : nil
        return ModelFiles(ggml: location(of: whisper), coreMLEncoder: encoder)
    }

    public func install(verifiedFile file: URL, as entry: ModelEntry) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try excludeFromBackup(root)
        let target = location(of: entry)
        if FileManager.default.fileExists(atPath: target.path) {
            try FileManager.default.removeItem(at: target)
        }
        if entry.kind == .coreMLEncoder {
            let staging = downloadsDirectory.appendingPathComponent("extract-\(UUID().uuidString)", isDirectory: true)
            defer { try? FileManager.default.removeItem(at: staging) }
            try ZipExtractor.extract(file, to: staging)
            let extracted = staging.appendingPathComponent(entry.installedName)
            guard FileManager.default.fileExists(atPath: extracted.path) else {
                throw ModelError.invalidArchive("\(entry.installedName) not found")
            }
            try FileManager.default.moveItem(at: extracted, to: target)
            try FileManager.default.removeItem(at: file)
        } else {
            try FileManager.default.moveItem(at: file, to: target)
        }
        try excludeFromBackup(target)
    }

    public func remove(_ entry: ModelEntry) throws {
        let url = location(of: entry)
        if FileManager.default.fileExists(atPath: url.path) {
            try FileManager.default.removeItem(at: url)
        }
    }

    public func availableDiskSpace() -> Int64 {
        let probe = FileManager.default.fileExists(atPath: root.path) ? root : root.deletingLastPathComponent()
        let values = try? probe.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        return values?.volumeAvailableCapacityForImportantUsage ?? 0
    }

    public func requiredSpace(for entry: ModelEntry) -> Int64 {
        entry.kind == .coreMLEncoder ? entry.size * 2 + entry.size / 10 : entry.size
    }

    private func excludeFromBackup(_ url: URL) throws {
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        var target = url
        try target.setResourceValues(values)
    }
}
