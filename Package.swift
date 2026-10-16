// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SillokKit",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "SillokCore", targets: ["SillokCore"]),
        .library(name: "SillokWhisper", targets: ["SillokWhisper"]),
    ],
    targets: [
        .target(name: "SillokCore"),

        .binaryTarget(name: "whisper", path: "Vendor/whisper.xcframework"),
        .target(name: "SillokWhisper", dependencies: ["SillokCore", "whisper"]),

        .executableTarget(
            name: "sillok-cli",
            dependencies: ["SillokCore", "SillokWhisper"],
            path: "Tools/sillok-cli"
        ),

        .testTarget(name: "SillokCoreTests", dependencies: ["SillokCore"]),
        .testTarget(name: "SillokWhisperTests", dependencies: ["SillokWhisper"]),
    ]
)
