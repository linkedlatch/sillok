import CryptoKit
import Foundation
import Synchronization
import Testing

@testable import SillokEngine
@testable import SillokStore

struct ModelInstallerTests {
    let root = FileManager.default.temporaryDirectory.appending(path: "SillokStoreTests-\(UUID().uuidString)")

    func source(_ name: String, contents: Data) throws -> CatalogFile {
        let sources = root.appending(path: "sources")
        try FileManager.default.createDirectory(at: sources, withIntermediateDirectories: true)
        let url = sources.appending(path: name)
        try contents.write(to: url)
        let hash = SHA256.hash(data: contents).map { String(format: "%02x", $0) }.joined()
        return CatalogFile(file: name, url: url, size: Int64(contents.count), sha256: hash, zip: false)
    }

    @Test func recommendedModelsAreAllPendingInAnEmptyDirectory() throws {
        let models = try Catalog.engineModels()
        let pending = try ModelInstaller.pendingFiles(for: models, in: root.appending(path: "models"))
        #expect(pending.count == Set(models.flatMap(\.files).map(\.file)).count)
        #expect(pending.allSatisfy { $0.needsDownload })
    }

    @Test func downloadsAndVerifiesAFile() async throws {
        let file = try source("model.bin", contents: Data(repeating: 7, count: 3 << 20))
        let models = root.appending(path: "models")
        let reports = Mutex<[InstallProgress]>([])
        try await ModelInstaller.install([PendingFile(file: file, needsDownload: true)], into: models) { progress in
            reports.withLock { $0.append(progress) }
        }
        let installed = try Data(contentsOf: models.appending(path: "model.bin"))
        #expect(installed.count == 3 << 20)
        #expect(reports.withLock { $0.contains { $0.phase == .downloading } })
        #expect(reports.withLock { $0.last?.phase } == .verifying)
        #expect(reports.withLock { $0.last?.completedBytes } == Int64(3 << 20))
    }

    @Test func rejectsADamagedFile() async throws {
        let good = try source("model.bin", contents: Data(repeating: 1, count: 1024))
        let damaged = CatalogFile(file: good.file, url: good.url, size: good.size,
                                  sha256: String(repeating: "0", count: 64), zip: false)
        let models = root.appending(path: "models")
        await #expect(throws: ModelInstallError.wrongHash(file: "model.bin")) {
            try await ModelInstaller.install([PendingFile(file: damaged, needsDownload: true)], into: models) { _ in }
        }
        #expect(!FileManager.default.fileExists(atPath: models.appending(path: "model.bin").path()))
        #expect(!FileManager.default.fileExists(atPath: models.appending(path: "model.bin.partial").path()))
    }

    @Test func replacesAPartialFileTheServerDoesNotResume() async throws {
        let file = try source("model.bin", contents: Data(repeating: 9, count: 2 << 20))
        let models = root.appending(path: "models")
        try FileManager.default.createDirectory(at: models, withIntermediateDirectories: true)
        try Data(repeating: 0, count: 1 << 20).write(to: models.appending(path: "model.bin.partial"))
        try await ModelInstaller.install([PendingFile(file: file, needsDownload: true)], into: models) { _ in }
        let installed = try Data(contentsOf: models.appending(path: "model.bin"))
        #expect(installed == Data(repeating: 9, count: 2 << 20))
        let leftovers = try FileManager.default.contentsOfDirectory(atPath: models.path())
        #expect(leftovers == ["model.bin"])
    }

    @Test func extractsAnArchiveAndRemovesIt() async throws {
        let bundle = root.appending(path: "build/encoder.mlmodelc")
        try FileManager.default.createDirectory(at: bundle, withIntermediateDirectories: true)
        try Data("weights".utf8).write(to: bundle.appending(path: "weights.bin"))
        let archive = root.appending(path: "build/encoder.mlmodelc.zip")
        let ditto = try Process.run(URL(filePath: "/usr/bin/ditto"),
                                    arguments: ["-c", "-k", "--keepParent", bundle.path(), archive.path()])
        ditto.waitUntilExit()
        let plain = try source("encoder.mlmodelc.zip", contents: Data(contentsOf: archive))
        let file = CatalogFile(file: plain.file, url: plain.url, size: plain.size, sha256: plain.sha256, zip: true)

        let models = root.appending(path: "models")
        try await ModelInstaller.install([PendingFile(file: file, needsDownload: true)], into: models) { _ in }
        let weights = try String(contentsOf: models.appending(path: "encoder.mlmodelc/weights.bin"), encoding: .utf8)
        #expect(weights == "weights")
        #expect(!FileManager.default.fileExists(atPath: models.appending(path: "encoder.mlmodelc.zip").path()))
        let leftovers = try FileManager.default.contentsOfDirectory(atPath: models.path())
        #expect(leftovers == ["encoder.mlmodelc"])
    }
}
