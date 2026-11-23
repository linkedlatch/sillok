import Testing

@testable import SillokEngine

@Test func versionIsReported() {
    #expect(!Engine.version.isEmpty)
}
