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
            ("large-v3-turbo-q5_0", .whisper, "Whisper large-v3-turbo (q5_0)", "ggml-large-v3-turbo-q5_0.bin", "large-v3-turbo-encoder"),
            ("large-v3-turbo-encoder", .coreMLEncoder, "large-v3-turbo Core ML encoder", "ggml-large-v3-turbo-encoder.mlmodelc.zip", nil),
        ]
    ),
    Source(
        repo: "ggml-org/whisper-vad",
        commit: "9ffd54a1e1ee413ddf265af9913beaf518d1639b",
        license: "MIT",
        files: [("silero-v6.2.0", .vad, "Silero VAD v6.2.0", "ggml-silero-v6.2.0.bin", nil)]
    ),
    Source(
        repo: "nvidia/Nemotron-3-Diarization",
        commit: "f667ed73aee57d40cc39428eb768b4fd87a0a29e",
        license: "OpenMDW-1.1",
        files: [("nemotron-3-diarization-q8_0", .diarization, "Nemotron 3 Diarization (q8_0)", "Nemotron-3-Diarization.q8_0.gguf", nil)]
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
