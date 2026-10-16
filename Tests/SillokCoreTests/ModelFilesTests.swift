import Testing
@testable import SillokCore

@Test(arguments: [
    ("ggml-large-v3-turbo-q8_0.bin", "ggml-large-v3-turbo-encoder.mlmodelc"),
    ("ggml-large-v3-turbo-q5_0.bin", "ggml-large-v3-turbo-encoder.mlmodelc"),
    ("ggml-large-v3-q5_0.bin", "ggml-large-v3-encoder.mlmodelc"),
    ("ggml-small-q5_1.bin", "ggml-small-encoder.mlmodelc"),
    ("ggml-tiny.bin", "ggml-tiny-encoder.mlmodelc"),
    ("ggml-large-v3-turbo.bin", "ggml-large-v3-turbo-encoder.mlmodelc"),
])
func coreMLEncoderNameFollowsWhisperRule(ggml: String, encoder: String) {
    #expect(ModelFiles.coreMLEncoderName(forGGML: ggml) == encoder)
}
