import Foundation
import SillokCore

struct TreeItem: Decodable {
    struct LFS: Decodable {
        var oid: String
        var size: Int64
    }
    var path: String
    var lfs: LFS?
}

struct Source {
    var repo: String
    var commit: String
    var license: String
    var files: [(id: String, kind: ModelEntry.Kind, name: String, file: String, encoder: String?)]
}

let sources = [
    Source(
        repo: "ggerganov/whisper.cpp",
        commit: "5359861c739e955e79d9a303bcbc70fb988958b1",
        license: "MIT",
        files: [
            ("large-v3-turbo-q8_0", .whisper, "Whisper large-v3-turbo (q8_0)", "ggml-large-v3-turbo-q8_0.bin", "large-v3-turbo-encoder"),
            ("large-v3-turbo-q5_0", .whisper, "Whisper large-v3-turbo (q5_0)", "ggml-large-v3-turbo-q5_0.bin", "large-v3-turbo-encoder"),
            ("large-v3-q5_0", .whisper, "Whisper large-v3 (q5_0)", "ggml-large-v3-q5_0.bin", "large-v3-encoder"),
            ("small-q5_1", .whisper, "Whisper small (q5_1)", "ggml-small-q5_1.bin", nil),
            ("large-v3-turbo-encoder", .coreMLEncoder, "large-v3-turbo Core ML 인코더", "ggml-large-v3-turbo-encoder.mlmodelc.zip", nil),
            ("large-v3-encoder", .coreMLEncoder, "large-v3 Core ML 인코더", "ggml-large-v3-encoder.mlmodelc.zip", nil),
        ]
    ),
    Source(
        repo: "ggml-org/whisper-vad",
        commit: "9ffd54a1e1ee413ddf265af9913beaf518d1639b",
        license: "MIT",
        files: [("silero-v6.2.0", .vad, "Silero VAD v6.2.0", "ggml-silero-v6.2.0.bin", nil)]
    ),
]

guard CommandLine.arguments.count == 2 else {
    print("usage: manifest-tool <output manifest.json>")
    exit(1)
}

var entries: [ModelEntry] = []
for source in sources {
    let api = URL(string: "https://huggingface.co/api/models/\(source.repo)/tree/\(source.commit)")!
    let (data, _) = try await URLSession.shared.data(from: api)
    let tree = Dictionary(uniqueKeysWithValues: try JSONDecoder().decode([TreeItem].self, from: data).map { ($0.path, $0) })
    for file in source.files {
        guard let lfs = tree[file.file]?.lfs else {
            print("missing \(file.file) in \(source.repo)")
            exit(1)
        }
        entries.append(ModelEntry(
            id: file.id,
            kind: file.kind,
            displayName: file.name,
            fileName: file.file,
            size: lfs.size,
            sha256: lfs.oid,
            urls: [URL(string: "https://huggingface.co/\(source.repo)/resolve/\(source.commit)/\(file.file)")!],
            coreMLEncoderID: file.encoder,
            license: source.license
        ))
    }
}

let encoder = JSONEncoder()
encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
try encoder.encode(ModelManifest(version: 1, models: entries)).write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
print("wrote \(entries.count) models")
