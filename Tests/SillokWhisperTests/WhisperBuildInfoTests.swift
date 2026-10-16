import Testing
@testable import SillokWhisper

@Test func buildIncludesMetalAndCoreML() {
    #expect(WhisperBuildInfo.backendNames.contains("MTL"))
    #expect(WhisperBuildInfo.systemInfo.contains("COREML = 1"))
}
