// swift-tools-version: 6.0
import PackageDescription

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
            name: "CSillok",
            dependencies: ["whisper"],
            path: "Native",
            exclude: ["CMakeLists.txt", "tests", "third_party/nlohmann/LICENSE.MIT", "third_party/doctest"],
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [.headerSearchPath("third_party")],
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
