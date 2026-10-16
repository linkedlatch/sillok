// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SillokKit",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "SillokCore", targets: ["SillokCore"]),
        .library(name: "SillokAudio", targets: ["SillokAudio"]),
        .library(name: "SillokWhisper", targets: ["SillokWhisper"]),
        .library(name: "SillokPlatform", targets: ["SillokPlatform"]),
        .library(name: "SillokModels", targets: ["SillokModels"]),
    ],
    targets: [
        .target(name: "SillokCore"),
        .target(name: "SillokAudio", dependencies: ["SillokCore"]),
        .target(name: "SillokPlatform", dependencies: ["SillokCore"]),
        .target(name: "SillokModels", dependencies: ["SillokCore"]),

        .binaryTarget(name: "whisper", path: "Vendor/whisper.xcframework"),
        .target(name: "SillokWhisper", dependencies: ["SillokCore", "whisper"]),

        .executableTarget(
            name: "sillok-cli",
            dependencies: ["SillokCore", "SillokAudio", "SillokWhisper", "SillokPlatform"],
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
        .testTarget(name: "SillokWhisperTests", dependencies: ["SillokWhisper", "SillokAudio"]),
    ]
)
