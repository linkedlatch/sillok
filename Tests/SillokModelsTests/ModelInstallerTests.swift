import CryptoKit
import Foundation
import Testing
import SillokCore
@testable import SillokModels

private func temporaryDirectory() -> URL {
    FileManager.default.temporaryDirectory.appendingPathComponent("sillok-models-\(UUID().uuidString)", isDirectory: true)
}

private func sha256(_ data: Data) -> String {
    SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
}

private func payload(_ size: Int) -> Data {
    Data((0..<size).map { UInt8(truncatingIfNeeded: $0 &* 31 &+ 7) })
}

private func entry(_ body: Data, urls: [String], sha: String? = nil) -> ModelEntry {
    ModelEntry(id: "test", kind: .whisper, displayName: "test", fileName: "ggml-test.bin",
               size: Int64(body.count), sha256: sha ?? sha256(body), urls: urls.map { URL(string: $0)! }, license: "MIT")
}

private func installer(_ entries: [ModelEntry]) -> ModelInstaller {
    ModelInstaller(
        store: ModelStore(root: temporaryDirectory(), manifest: ModelManifest(version: 1, models: entries)),
        downloader: ResumableDownloader(configuration: StubServer.configuration)
    )
}

@Suite(.serialized)
struct ModelInstallerTests {
    init() { StubServer.reset() }

    @Test func downloadVerifiesAndInstallsExcludedFromBackup() async throws {
        let body = payload(300_000)
        StubServer.routes["https://a.test/m.bin"] = .init(body: body)
        let model = entry(body, urls: ["https://a.test/m.bin"])
        let installer = installer([model])

        try await installer.download(model)

        let installed = installer.store.location(of: model)
        #expect(try Data(contentsOf: installed) == body)
        #expect(installer.store.isInstalled(model))
        #expect(!FileManager.default.fileExists(atPath: installer.store.partURL(for: model).path))
        #expect(try installed.resourceValues(forKeys: [.isExcludedFromBackupKey]).isExcludedFromBackup == true)
    }

    @Test func interruptedDownloadResumesWithRange() async throws {
        let body = payload(500_000)
        StubServer.routes["https://a.test/m.bin"] = .init(body: body, failAfter: 120_000)
        let model = entry(body, urls: ["https://a.test/m.bin"])
        let installer = installer([model])

        await #expect(throws: ModelError.allSourcesFailed("ggml-test.bin")) { try await installer.download(model) }
        let partSize = try FileManager.default.attributesOfItem(atPath: installer.store.partURL(for: model).path)[.size] as? Int
        #expect(partSize == 120_000)

        try await installer.download(model)
        #expect(StubServer.requests.last?.value(forHTTPHeaderField: "Range") == "bytes=120000-")
        #expect(try Data(contentsOf: installer.store.location(of: model)) == body)
    }

    @Test func checksumMismatchIsNotInstalled() async throws {
        let body = payload(10_000)
        StubServer.routes["https://a.test/m.bin"] = .init(body: body)
        let model = entry(body, urls: ["https://a.test/m.bin"], sha: String(repeating: "0", count: 64))
        let installer = installer([model])

        await #expect(throws: ModelError.checksumMismatch("ggml-test.bin")) { try await installer.download(model) }
        #expect(!installer.store.isInstalled(model))
        #expect(!FileManager.default.fileExists(atPath: installer.store.partURL(for: model).path))
    }

    @Test func fallsBackToNextSource() async throws {
        let body = payload(50_000)
        StubServer.routes["https://mirror.test/m.bin"] = .init(body: body)
        let model = entry(body, urls: ["https://primary.test/m.bin", "https://mirror.test/m.bin"])
        let installer = installer([model])

        try await installer.download(model)
        #expect(StubServer.requests.map { $0.url!.host() } == ["primary.test", "mirror.test"])
        #expect(installer.store.isInstalled(model))
    }

    @Test func serverIgnoringRangeRestartsFromZero() async throws {
        let body = payload(200_000)
        StubServer.routes["https://a.test/m.bin"] = .init(body: body, failAfter: 80_000, ignoresRange: true)
        let model = entry(body, urls: ["https://a.test/m.bin"])
        let installer = installer([model])

        _ = try? await installer.download(model)
        try await installer.download(model)
        #expect(try Data(contentsOf: installer.store.location(of: model)) == body)
    }

    @Test func importRecognizesManifestFileByHash() throws {
        let body = payload(20_000)
        let model = entry(body, urls: [])
        let installer = installer([model])
        let source = temporaryDirectory().appendingPathComponent("downloaded.bin")
        try FileManager.default.createDirectory(at: source.deletingLastPathComponent(), withIntermediateDirectories: true)
        try body.write(to: source)

        #expect(try installer.importFile(source) == model)
        #expect(installer.store.isInstalled(model))
        #expect(FileManager.default.fileExists(atPath: source.path))

        let unknown = source.deletingLastPathComponent().appendingPathComponent("other.bin")
        try payload(10).write(to: unknown)
        #expect(throws: ModelError.unknownModelFile) { try installer.importFile(unknown) }
    }

    @Test func insufficientDiskSpaceIsReportedBeforeDownloading() async throws {
        var model = entry(payload(1), urls: ["https://a.test/m.bin"])
        model.size = Int64.max / 4
        await #expect(throws: ModelError.self) { try await installer([model]).download(model) }
        #expect(StubServer.requests.isEmpty)
    }
}
