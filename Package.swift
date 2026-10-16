// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SillokKit",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "SillokCore", targets: ["SillokCore"]),
        .library(name: "SillokAudio", targets: ["SillokAudio"]),
        .library(name: "SillokWhisper", targets: ["SillokWhisper"]),
    ],
    targets: [
        .target(name: "SillokCore"),
        .target(name: "SillokAudio", dependencies: ["SillokCore"]),

        .binaryTarget(name: "whisper", path: "Vendor/whisper.xcframework"),
        .target(name: "SillokWhisper", dependencies: ["SillokCore", "whisper"]),

        .executableTarget(
            name: "sillok-cli",
            dependencies: ["SillokCore", "SillokAudio", "SillokWhisper"],
            path: "Tools/sillok-cli"
        ),

        .testTarget(name: "SillokCoreTests", dependencies: ["SillokCore"]),
        .testTarget(name: "SillokAudioTests", dependencies: ["SillokAudio"]),
        .testTarget(name: "SillokWhisperTests", dependencies: ["SillokWhisper", "SillokAudio"]),
    ]
)
