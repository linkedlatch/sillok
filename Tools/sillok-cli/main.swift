import SillokWhisper

let args = CommandLine.arguments.dropFirst()

switch args.first {
case "info":
    print("whisper.cpp \(WhisperBuildInfo.version)")
    print("backends: \(WhisperBuildInfo.backendNames.joined(separator: ", "))")
    print(WhisperBuildInfo.systemInfo)
default:
    print("usage: sillok-cli info")
}
