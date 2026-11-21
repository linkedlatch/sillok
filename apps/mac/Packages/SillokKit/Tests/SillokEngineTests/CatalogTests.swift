import Foundation
import Testing

@testable import SillokEngine

@Test func catalogHasOneRecommendedModelPerRole() throws {
    let models = try Catalog.models()
    for role in ModelRole.allCases {
        #expect(models.filter { $0.role == role && $0.recommended }.count == 1)
    }
    let transcription = models.filter { $0.role == .transcription }
    #expect(transcription.allSatisfy { $0.languages?.isEmpty == false && $0.memory != nil })
}

@Test func engineNeedsOneModelPerRole() throws {
    let models = try Catalog.engineModels()
    #expect(Set(models.map(\.role)) == Set(ModelRole.allCases))
}

@Test func unknownTranscriptionModelIsRejected() {
    #expect(throws: SillokError.self) {
        try Catalog.engineModels(transcriptionModel: "no-such-model")
    }
}

@Test func emptyDirectoryHasNoModels() throws {
    let directory = FileManager.default.temporaryDirectory.appending(path: UUID().uuidString)
    let statuses = try Catalog.check(directory: directory)
    #expect(!statuses.isEmpty)
    #expect(statuses.allSatisfy { $0.status == .missing })
}
