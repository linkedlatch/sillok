import Foundation

public struct ProjectStore: Sendable {
    public let root: URL

    public init(root: URL) {
        self.root = root
    }

    public func directory(for id: UUID) -> URL {
        root.appendingPathComponent(id.uuidString, isDirectory: true)
    }

    public func projectURL(for id: UUID) -> URL {
        directory(for: id).appendingPathComponent("project.json")
    }

    public func checkpointURL(for id: UUID) -> URL {
        directory(for: id).appendingPathComponent("checkpoint.json")
    }

    public func save(_ project: TranscriptProject) throws {
        try VersionedStore.save(project, to: projectURL(for: project.id))
    }

    public func load(_ id: UUID) throws -> TranscriptProject {
        try VersionedStore.load(TranscriptProject.self, from: projectURL(for: id))
    }

    public func loadAll() -> [TranscriptProject] {
        let ids = (try? FileManager.default.contentsOfDirectory(atPath: root.path)) ?? []
        return ids.compactMap(UUID.init(uuidString:))
            .compactMap { try? load($0) }
            .sorted { $0.createdAt > $1.createdAt }
    }

    public func delete(_ id: UUID) throws {
        try FileManager.default.removeItem(at: directory(for: id))
    }
}
