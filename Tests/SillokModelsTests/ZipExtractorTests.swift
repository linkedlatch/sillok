import Foundation
import Testing
import SillokCore
@testable import SillokModels

private let repoRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

private func temporaryDirectory() throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("sillok-zip-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

private func run(_ tool: String, _ arguments: [String], in directory: URL) throws {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: tool)
    process.arguments = arguments
    process.currentDirectoryURL = directory
    process.standardOutput = FileHandle.nullDevice
    process.standardError = FileHandle.nullDevice
    try process.run()
    process.waitUntilExit()
    guard process.terminationStatus == 0 else { throw CocoaError(.executableLoad) }
}

private func makeTree(in root: URL) throws -> [String: Data] {
    let files: [String: Data] = [
        "model.mlmodelc/metadata.json": Data(#"{"name":"테스트"}"#.utf8),
        "model.mlmodelc/weights/weight.bin": Data((0..<3_000_000).map { UInt8(truncatingIfNeeded: $0 % 251) }),
        "model.mlmodelc/empty.bin": Data(),
    ]
    for (path, data) in files {
        let url = root.appendingPathComponent(path)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url)
    }
    return files
}

@Test(arguments: [["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", "model.mlmodelc", "../out.zip"],
                  ["/usr/bin/zip", "-0", "-r", "-q", "../out.zip", "model.mlmodelc"]])
func extractsDeflatedAndStoredArchives(command: [String]) throws {
    let work = try temporaryDirectory()
    let source = work.appendingPathComponent("src", isDirectory: true)
    let files = try makeTree(in: source)
    try run(command[0], Array(command.dropFirst()), in: source)

    let destination = work.appendingPathComponent("dst", isDirectory: true)
    try ZipExtractor.extract(work.appendingPathComponent("out.zip"), to: destination)
    for (path, data) in files {
        #expect(try Data(contentsOf: destination.appendingPathComponent(path)) == data)
    }
}

@Test func rejectsPathTraversal() throws {
    let work = try temporaryDirectory()
    let zip = work.appendingPathComponent("evil.zip")
    try run("/usr/bin/python3", ["-I", "-c", "import zipfile; z=zipfile.ZipFile('\(zip.path)','w'); z.writestr('../evil.txt','x'); z.close()"], in: work)
    #expect(throws: ModelError.invalidArchive("unsafe path ../evil.txt")) {
        try ZipExtractor.extract(zip, to: work.appendingPathComponent("dst"))
    }
}

@Test(.enabled(if: FileManager.default.fileExists(atPath: repoRoot.appendingPathComponent("models/ggml-tiny-encoder.mlmodelc.zip").path)))
func installsRealCoreMLEncoderArchive() throws {
    let zip = repoRoot.appendingPathComponent("models/ggml-tiny-encoder.mlmodelc.zip")
    let encoder = ModelEntry(id: "tiny-encoder", kind: .coreMLEncoder, displayName: "tiny", fileName: "ggml-tiny-encoder.mlmodelc.zip",
                             size: 15_037_446, sha256: "", urls: [], license: "MIT")
    let store = ModelStore(root: try temporaryDirectory(), manifest: ModelManifest(version: 1, models: [encoder]))
    let copy = store.root.appendingPathComponent("copy.zip")
    try FileManager.default.copyItem(at: zip, to: copy)

    try store.install(verifiedFile: copy, as: encoder)

    #expect(store.isInstalled(encoder))
    let installed = store.location(of: encoder)
    #expect(installed.lastPathComponent == "ggml-tiny-encoder.mlmodelc")
    let reference = repoRoot.appendingPathComponent("models/ggml-tiny-encoder.mlmodelc")
    for name in ["model.mil", "coremldata.bin", "weights/weight.bin", "metadata.json"] {
        #expect(FileManager.default.contentsEqual(atPath: installed.appendingPathComponent(name).path,
                                                  andPath: reference.appendingPathComponent(name).path))
    }
    #expect(!FileManager.default.fileExists(atPath: store.root.appendingPathComponent("__MACOSX").path))
}

@Test func bundledManifestPairsWhisperModelsWithMatchingEncoders() throws {
    let url = repoRoot.appendingPathComponent("Apps/Sillok/Resources/manifest.json")
    let manifest = try JSONDecoder().decode(ModelManifest.self, from: Data(contentsOf: url))
    #expect(manifest.entry("large-v3-turbo-q8_0") != nil)
    #expect(manifest.entry("silero-v6.2.0")?.kind == .vad)
    for whisper in manifest.models where whisper.coreMLEncoderID != nil {
        let encoder = try #require(manifest.coreMLEncoder(for: whisper))
        #expect(encoder.kind == .coreMLEncoder)
        #expect(encoder.installedName == ModelFiles.coreMLEncoderName(forGGML: whisper.fileName))
    }
    #expect(manifest.models.allSatisfy { $0.sha256.count == 64 && $0.size > 0 && !$0.urls.isEmpty })
}
