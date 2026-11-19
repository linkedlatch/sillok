// swift-tools-version: 6.0
import PackageDescription

let ffmpeg = ["avformat", "avcodec", "avfilter", "swresample", "avutil"]

let package = Package(
    name: "SillokKit",
    platforms: [.macOS(.v15)],
    products: [
        .library(name: "SillokKit", targets: ["SillokEngine"])
    ],
    targets: [
        .binaryTarget(name: "CSillok", path: "Frameworks/CSillok.xcframework"),
        .target(
            name: "SillokEngine",
            dependencies: ["CSillok"] + ffmpeg.map { .target(name: $0) }
        ),
        .testTarget(name: "SillokEngineTests", dependencies: ["SillokEngine"]),
    ] + ffmpeg.map { .binaryTarget(name: $0, path: "Frameworks/\($0).xcframework") }
)
