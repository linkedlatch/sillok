import Foundation
import Testing
@testable import SillokCore

private struct DocumentV2: VersionedDocument, Equatable {
    static let currentSchemaVersion = 2
    var schemaVersion: Int
    var title: String
    var speakerNames: [String]

    static func migrate(_ json: inout [String: Any], from version: Int) throws {
        if version == 1 { json["speakerNames"] = [String]() }
    }
}

private func temporaryURL() -> URL {
    FileManager.default.temporaryDirectory
        .appendingPathComponent("sillok-store-\(UUID().uuidString)")
        .appendingPathComponent("doc.json")
}

@Test func saveWritesCurrentVersionAndLoadsBack() throws {
    let url = temporaryURL()
    try VersionedStore.save(DocumentV2(schemaVersion: 0, title: "a", speakerNames: ["Speaker 1"]), to: url)
    let json = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any]
    #expect(json?["schemaVersion"] as? Int == 2)
    #expect(try VersionedStore.load(DocumentV2.self, from: url) == DocumentV2(schemaVersion: 2, title: "a", speakerNames: ["Speaker 1"]))
}

@Test func olderVersionIsMigrated() throws {
    let url = temporaryURL()
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data(#"{"schemaVersion":1,"title":"old"}"#.utf8).write(to: url)
    let loaded = try VersionedStore.load(DocumentV2.self, from: url)
    #expect(loaded == DocumentV2(schemaVersion: 2, title: "old", speakerNames: []))
}

@Test func newerVersionIsRejected() throws {
    let url = temporaryURL()
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data(#"{"schemaVersion":3,"title":"future","speakerNames":[]}"#.utf8).write(to: url)
    #expect(throws: VersionedStoreError.unsupportedVersion(3)) {
        try VersionedStore.load(DocumentV2.self, from: url)
    }
}

@Test func projectStoreRoundTripsProjects() throws {
    let store = ProjectStore(root: temporaryURL().deletingLastPathComponent())
    var project = TranscriptProject(sourceName: "서강대길.m4a", source: .bookmark(Data([1, 2, 3])), options: TranscriptionOptions(languageMode: .koreanEnglish))
    project.status = .completed
    project.segments = [Segment(start: 0, end: 1, text: "안녕", language: "ko")]
    try store.save(project)
    #expect(try store.load(project.id) == project)
    #expect(store.loadAll().map(\.id) == [project.id])
    try store.delete(project.id)
    #expect(store.loadAll().isEmpty)
}
