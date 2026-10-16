import Foundation
import Testing
import SillokCore
@testable import SillokModels

private let repoRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let manifest = try! JSONDecoder().decode(ModelManifest.self, from: Data(contentsOf: repoRoot.appendingPathComponent("Apps/Sillok/Resources/manifest.json")))

@Test func defaultSelectionNeedsTurboEncoderAndVAD() {
    let ids = ModelSelection.requiredEntries(whisperID: ModelSelection.defaultWhisperID, useCoreML: true, manifest: manifest).map(\.id)
    #expect(ids == ["large-v3-turbo-q8_0", "large-v3-turbo-encoder", "silero-v6.2.0"])
    let withoutCoreML = ModelSelection.requiredEntries(whisperID: ModelSelection.defaultWhisperID, useCoreML: false, manifest: manifest).map(\.id)
    #expect(withoutCoreML == ["large-v3-turbo-q8_0", "silero-v6.2.0"])
}

@Test func coreMLNeedsRoomForBothModelsAndOverhead() throws {
    let whisper = try #require(manifest.entry("large-v3-turbo-q8_0"))
    let encoder = manifest.coreMLEncoder(for: whisper)
    let needed = whisper.size + encoder!.size + ModelSelection.runtimeOverhead
    #expect(ModelSelection.shouldUseCoreML(whisper: whisper, encoder: encoder, availableMemory: needed, countEncoder: true))
    #expect(!ModelSelection.shouldUseCoreML(whisper: whisper, encoder: encoder, availableMemory: needed - 1, countEncoder: true))
    #expect(ModelSelection.shouldUseCoreML(whisper: whisper, encoder: encoder, availableMemory: needed - encoder!.size, countEncoder: false))
    #expect(!ModelSelection.shouldUseCoreML(whisper: whisper, encoder: nil, availableMemory: .max, countEncoder: false))
}
