// swift-tools-version: 6.0
import PackageDescription

let nemoHeaderPaths = ["asr", "asr/diar", "asr/encoder", "asr/features", "common", "runtime/ggml"]

let package = Package(
    name: "SillokKit",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "SillokCore", targets: ["SillokCore"]),
        .library(name: "SillokAudio", targets: ["SillokAudio"]),
        .library(name: "SillokNative", targets: ["SillokNative"]),
        .library(name: "SillokPlatform", targets: ["SillokPlatform"]),
        .library(name: "SillokModels", targets: ["SillokModels"]),
    ],
    targets: [
        .target(name: "SillokCore"),
        .target(name: "SillokAudio", dependencies: ["SillokCore"]),
        .target(name: "SillokPlatform", dependencies: ["SillokCore"]),
        .target(name: "SillokModels", dependencies: ["SillokCore"]),

        .binaryTarget(name: "whisper", path: "Vendor/whisper.xcframework"),
        .target(
            name: "CNemoDiar",
            dependencies: ["whisper"],
            path: "Vendor/NeMo-Speech.cpp",
            sources: [
                "src/asr/diar/aosc_state.cpp",
                "src/asr/diar/diar_pipeline.cpp",
                "src/asr/diar/diarizer.cpp",
                "src/asr/diar/sortformer_model.cpp",
                "src/asr/encoder/fastconformer.cpp",
                "src/asr/encoder/rel_pos_attention.cpp",
                "src/asr/encoder/rope_transformer.cpp",
                "src/asr/encoder/transformer_encoder.cpp",
                "src/asr/features/fe.cpp",
                "src/common/audio_resampler.cpp",
                "src/common/parameter_parser.cpp",
                "src/runtime/ggml/backend.cpp",
                "src/runtime/ggml/loader.cpp",
                "src/runtime/ggml/logging.cpp",
                "src/runtime/ggml/nn.cpp",
                "src/runtime/ggml/session.cpp",
                "src/runtime/ggml/tensor_container.cpp",
            ],
            cxxSettings: nemoHeaderPaths.map { .headerSearchPath("src/" + $0) }
        ),
        .target(
            name: "CSillok",
            dependencies: ["whisper", "CNemoDiar"],
            path: "Native",
            exclude: ["CMakeLists.txt", "tests", "third_party/nlohmann/LICENSE.MIT", "third_party/doctest"],
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [.headerSearchPath("third_party")] + nemoHeaderPaths.map { .headerSearchPath("../Vendor/NeMo-Speech.cpp/src/" + $0) },
            linkerSettings: [
                .linkedLibrary("z"),
                .linkedFramework("Accelerate"),
                .linkedFramework("CoreML"),
                .linkedFramework("Foundation"),
                .linkedFramework("Metal"),
            ]
        ),
        .target(name: "SillokNative", dependencies: ["SillokCore", "CSillok"]),

        .executableTarget(
            name: "sillok-cli",
            dependencies: ["SillokCore", "SillokAudio", "SillokNative", "SillokPlatform"],
            path: "Tools/sillok-cli"
        ),
        .executableTarget(
            name: "manifest-tool",
            dependencies: ["SillokCore"],
            path: "Tools/manifest-tool"
        ),

        .testTarget(name: "SillokCoreTests", dependencies: ["SillokCore"]),
        .testTarget(name: "SillokModelsTests", dependencies: ["SillokModels"]),
        .testTarget(name: "SillokAudioTests", dependencies: ["SillokAudio"]),
        .testTarget(name: "SillokNativeTests", dependencies: ["SillokNative", "SillokAudio"]),
    ],
    cxxLanguageStandard: .cxx20
)
