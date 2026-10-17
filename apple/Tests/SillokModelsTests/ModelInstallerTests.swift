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
        downloader: ModelDownloader(configuration: .ephemeral)
    )
}

private func stagingURL(_ installer: ModelInstaller, _ entry: ModelEntry) -> URL {
    ModelDownloader.stagingURL(for: entry, in: installer.store.downloadsDirectory)
}

private func resumeURL(_ installer: ModelInstaller, _ entry: ModelEntry) -> URL {
    ModelDownloader.resumeDataURL(forStaging: stagingURL(installer, entry))
}

private func exists(_ url: URL) -> Bool {
    FileManager.default.fileExists(atPath: url.path)
}

private final class Counter: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Int64 = 0
    var current: Int64 { lock.withLock { value } }
    func set(_ newValue: Int64) { lock.withLock { value = newValue } }
}

@Suite(.serialized)
struct ModelInstallerTests {
    @Test func downloadVerifiesAndInstallsExcludedFromBackup() async throws {
        let server = try TestHTTPServer()
        try await server.start()
        let body = payload(300_000)
        server.route("/m.bin", .init(body: body))
        let model = entry(body, urls: [server.url("/m.bin")])
        let installer = installer([model])
        let received = Counter()

        try await installer.download(model) { bytes, _ in received.set(bytes) }

        let installed = installer.store.location(of: model)
        #expect(try Data(contentsOf: installed) == body)
        #expect(installer.store.isInstalled(model))
        #expect(received.current == Int64(body.count))
        #expect(!exists(stagingURL(installer, model)))
        #expect(!exists(resumeURL(installer, model)))
        #expect(try installed.resourceValues(forKeys: [.isExcludedFromBackupKey]).isExcludedFromBackup == true)
    }

    @Test func interruptedDownloadResumesWithRange() async throws {
        let server = try TestHTTPServer()
        try await server.start()
        let body = payload(500_000)
        server.route("/m.bin", .init(body: body, cutAfter: 120_000))
        let model = entry(body, urls: [server.url("/m.bin")])
        let installer = installer([model])

        await #expect(throws: ModelError.allSourcesFailed("ggml-test.bin")) { try await installer.download(model) }
        #expect(exists(resumeURL(installer, model)))
        #expect(server.requests.count == 1)

        try await installer.download(model)
        #expect(server.requests.count == 2)
        #expect(server.requests.last?.headers["range"] == "bytes=120000-")
        #expect(try Data(contentsOf: installer.store.location(of: model)) == body)
        #expect(!exists(resumeURL(installer, model)))
    }

    @Test func checksumMismatchIsNotInstalled() async throws {
        let server = try TestHTTPServer()
        try await server.start()
        let body = payload(10_000)
        server.route("/m.bin", .init(body: body))
        let model = entry(body, urls: [server.url("/m.bin")], sha: String(repeating: "0", count: 64))
        let installer = installer([model])

        await #expect(throws: ModelError.checksumMismatch("ggml-test.bin")) { try await installer.download(model) }
        #expect(!installer.store.isInstalled(model))
        #expect(!exists(stagingURL(installer, model)))
        #expect(!exists(resumeURL(installer, model)))
    }

    @Test func fallsBackToNextSource() async throws {
        let server = try TestHTTPServer()
        try await server.start()
        let body = payload(50_000)
        server.route("/mirror.bin", .init(body: body))
        let model = entry(body, urls: [server.url("/missing.bin"), server.url("/mirror.bin")])
        let installer = installer([model])

        try await installer.download(model)
        #expect(server.requests.map(\.path) == ["/missing.bin", "/mirror.bin"])
        #expect(installer.store.isInstalled(model))
    }

    @Test func cancellationProducesResumeData() async throws {
        let server = try TestHTTPServer()
        try await server.start()
        let body = payload(400_000)
        server.route("/m.bin", .init(body: body, cutAfter: 100_000, stalls: true))
        let model = entry(body, urls: [server.url("/m.bin")])
        let installer = installer([model])
        let received = Counter()

        let task = Task { try await installer.download(model) { bytes, _ in received.set(bytes) } }
        let deadline = ContinuousClock.now + .seconds(10)
        while received.current < 100_000, ContinuousClock.now < deadline {
            try await Task.sleep(for: .milliseconds(10))
        }
        task.cancel()

        await #expect(throws: CancellationError.self) { try await task.value }
        #expect(exists(resumeURL(installer, model)))
        #expect(!installer.store.isInstalled(model))

        try await installer.download(model)
        #expect(server.requests.last?.headers["range"] == "bytes=100000-")
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
        let server = try TestHTTPServer()
        try await server.start()
        var model = entry(payload(1), urls: [server.url("/m.bin")])
        model.size = Int64.max / 4
        await #expect(throws: ModelError.self) { try await installer([model]).download(model) }
        #expect(server.requests.isEmpty)
    }
}
