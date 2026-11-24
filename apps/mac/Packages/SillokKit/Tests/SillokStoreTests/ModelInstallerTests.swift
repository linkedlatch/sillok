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

    @Test func resumesAPartialFileAndCountsOnlyWhatIsLeft() async throws {
        let contents = Data((0..<(2 << 20)).map { UInt8($0 % 251) })
        let local = try source("model.bin", contents: contents)
        let file = CatalogFile(file: local.file, url: URL(string: "ranged://models/model.bin")!, size: local.size,
                               sha256: local.sha256, zip: false)
        RangedProtocol.contents.withLock { $0 = contents }
        let models = root.appending(path: "models")
        try FileManager.default.createDirectory(at: models, withIntermediateDirectories: true)
        try contents.prefix(1 << 20).write(to: models.appending(path: "model.bin.partial"))
        #expect(ModelInstaller.downloaded(file, in: models) == 1 << 20)

        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [RangedProtocol.self]
        let reports = Mutex<[InstallProgress]>([])
        let pending = PendingFile(file: file, needsDownload: true, downloadedBytes: 1 << 20)
        try await ModelInstaller.install([pending], into: models, session: URLSession(configuration: configuration)) {
            progress in reports.withLock { $0.append(progress) }
        }
        #expect(try Data(contentsOf: models.appending(path: "model.bin")) == contents)
        #expect(RangedProtocol.ranges.withLock { $0 } == ["bytes=\(1 << 20)-"])
        let downloading = reports.withLock { $0.filter { $0.phase == .downloading } }
        #expect(!downloading.isEmpty)
        #expect(downloading.allSatisfy { $0.totalBytes == 1 << 20 && $0.completedBytes <= 1 << 20 })
        #expect(reports.withLock { $0.last?.completedBytes } == Int64(1 << 20))
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

    @Test func anExtractionCancelledBeforeItStartsIsCancelled() async throws {
        let archive = root.appending(path: "encoder.mlmodelc.zip")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try Data().write(to: archive)
        let extraction = Task {
            withUnsafeCurrentTask { $0?.cancel() }
            try await ModelInstaller.extract(archive, file: "encoder.mlmodelc.zip")
        }
        await #expect(throws: CancellationError.self) { try await extraction.value }
        #expect(try FileManager.default.contentsOfDirectory(atPath: root.path()) == ["encoder.mlmodelc.zip"])
    }

    @Test(.timeLimit(.minutes(1))) func aDownloadCancelledBeforeItStartsIsCancelled() async throws {
        let file = try source("model.bin", contents: Data(repeating: 5, count: 1024))
        let partial = root.appending(path: "model.bin.partial")
        let download = Task {
            withUnsafeCurrentTask { $0?.cancel() }
            try await ModelInstaller.download(file, to: partial, session: .shared) { _ in }
        }
        await #expect(throws: CancellationError.self) { try await download.value }
    }

    @Test func removesAModelButKeepsFilesAnotherModelUses() throws {
        let catalog = try Catalog.models()
        let q5 = try #require(catalog.first { $0.id == "whisper-large-v3-turbo-q5_0" })
        let q8 = try #require(catalog.first { $0.id == "whisper-large-v3-turbo-q8_0" })
        let encoder = try #require(q5.files.first { $0.zip })
        let bundle = (encoder.file as NSString).deletingPathExtension
        let models = root.appending(path: "models")
        try FileManager.default.createDirectory(at: models.appending(path: bundle), withIntermediateDirectories: true)
        for file in (q5.files + q8.files).filter({ !$0.zip }) {
            try Data().write(to: models.appending(path: file.file))
        }

        try ModelInstaller.remove(q5, keepingFilesOf: [q8], from: models)
        var left = try Set(FileManager.default.contentsOfDirectory(atPath: models.path()))
        #expect(left == Set(q8.files.filter { !$0.zip }.map(\.file) + [bundle]))

        try ModelInstaller.remove(q8, keepingFilesOf: [], from: models)
        left = try Set(FileManager.default.contentsOfDirectory(atPath: models.path()))
        #expect(left.isEmpty)
    }

    @Test func aModelIsStartedOnlyByFilesOfItsOwn() throws {
        let qwen = try Catalog.models().filter { $0.backend == "qwen" && $0.role == .transcription }
        let q8 = try #require(qwen.first { $0.quantization == "q8_0" })
        let bf16 = try #require(qwen.first { $0.quantization == "bf16" })
        func isStarted(_ model: CatalogModel, besides other: CatalogModel, downloaded: [String: Int64]) -> Bool {
            let pending = model.files.filter { downloaded[$0.file] != $0.size }.map {
                PendingFile(file: $0, needsDownload: true, downloadedBytes: downloaded[$0.file] ?? 0)
            }
            return ModelInstaller.isStarted(model, sharingFilesWith: [other], pending: pending)
        }
        let first = q8.files[0]
        #expect(!bf16.files.contains(first))
        #expect(!isStarted(q8, besides: bf16, downloaded: [:]))
        #expect(isStarted(q8, besides: bf16, downloaded: [first.file: 1]))
        #expect(isStarted(q8, besides: bf16, downloaded: [first.file: first.size]))

        let complete = Dictionary(uniqueKeysWithValues: q8.files.map { ($0.file, $0.size) })
        #expect(bf16.files.contains { complete[$0.file] != nil })
        #expect(!isStarted(bf16, besides: q8, downloaded: complete))
    }

    @Test func removesAPartlyDownloadedModelButKeepsFilesAnotherModelUses() throws {
        let qwen = try Catalog.models().filter { $0.backend == "qwen" && $0.role == .transcription }
        let q8 = try #require(qwen.first { $0.quantization == "q8_0" })
        let bf16 = try #require(qwen.first { $0.quantization == "bf16" })
        let models = root.appending(path: "models")
        try FileManager.default.createDirectory(at: models, withIntermediateDirectories: true)
        for file in q8.files {
            try Data().write(to: models.appending(path: file.file))
        }
        try Data().write(to: models.appending(path: bf16.files[0].file + ".partial"))

        try ModelInstaller.remove(bf16, keepingFilesOf: [q8], from: models)
        let left = try Set(FileManager.default.contentsOfDirectory(atPath: models.path()))
        #expect(left == Set(q8.files.map(\.file)))
    }

    @Test func removesOnlyTheEncoder() throws {
        let model = try #require(try Catalog.models().first { $0.id == "whisper-large-v3-turbo-q5_0" })
        let encoder = try #require(model.files.first { $0.zip })
        let models = root.appending(path: "models")
        try FileManager.default.createDirectory(
            at: models.appending(path: (encoder.file as NSString).deletingPathExtension), withIntermediateDirectories: true)
        try Data().write(to: models.appending(path: encoder.file + ".partial"))
        for file in model.files where !file.zip {
            try Data().write(to: models.appending(path: file.file))
        }

        try ModelInstaller.remove([encoder], from: models)
        let left = try Set(FileManager.default.contentsOfDirectory(atPath: models.path()))
        #expect(left == Set(model.files.filter { !$0.zip }.map(\.file)))
    }
}

final class RangedProtocol: URLProtocol {
    static let contents = Mutex(Data())
    static let ranges = Mutex<[String]>([])

    override class func canInit(with request: URLRequest) -> Bool {
        request.url?.scheme == "ranged"
    }

    override class func canonicalRequest(for request: URLRequest) -> URLRequest {
        request
    }

    override func startLoading() {
        let data = Self.contents.withLock { $0 }
        let range = request.value(forHTTPHeaderField: "Range")
        Self.ranges.withLock { $0.append(range ?? "") }
        let start = range.flatMap { Int($0.dropFirst("bytes=".count).dropLast()) } ?? 0
        let response = HTTPURLResponse(url: request.url!, statusCode: range == nil ? 200 : 206,
                                       httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        var offset = start
        while offset < data.count {
            let end = min(offset + (64 << 10), data.count)
            client?.urlProtocol(self, didLoad: data.subdata(in: offset..<end))
            offset = end
        }
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
