import Darwin
import Foundation
import SillokAudio
import SillokCore
import SillokWhisper

func peakFootprintMB() -> Double {
    var info = task_vm_info_data_t()
    var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
    let result = withUnsafeMutablePointer(to: &info) {
        $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
        }
    }
    guard result == KERN_SUCCESS else { return 0 }
    return Double(info.ledger_phys_footprint_peak) / 1_048_576
}

func decode(path: String, window: TimeInterval) async throws {
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: path))
    let clock = ContinuousClock()
    var total = 0
    let elapsed = try await clock.measure {
        var start: TimeInterval = 0
        while start < source.duration {
            total += try await source.read(from: start, to: start + window).count
            start += window
        }
    }
    let seconds = Double(elapsed.components.seconds) + Double(elapsed.components.attoseconds) / 1e18
    print(String(format: "duration %.3fs  samples %d (%.3fs)  window %.0fs", source.duration, total, Double(total) / Double(PCMFormat.sampleRate), window))
    print(String(format: "elapsed %.2fs (%.0fx realtime)  peak footprint %.1f MB", seconds, source.duration / seconds, peakFootprintMB()))
}

func transcribe(arguments: [String]) async throws {
    var positional: [String] = []
    var options: [String: String] = [:]
    var flags: Set<String> = []
    var index = 0
    while index < arguments.count {
        let argument = arguments[index]
        if argument == "--cpu" {
            flags.insert(argument)
        } else if argument.hasPrefix("--"), index + 1 < arguments.count {
            options[argument] = arguments[index + 1]
            index += 1
        } else {
            positional.append(argument)
        }
        index += 1
    }
    guard positional.count == 2 else { throw CLIError.usage }

    let source = try await AssetAudioSource(url: URL(fileURLWithPath: positional[1]))
    let from = TimeInterval(options["--from"] ?? "") ?? 0
    let to = TimeInterval(options["--to"] ?? "") ?? min(source.duration, from + 30)
    let samples = try await source.read(from: from, to: to)

    let engine = WhisperCppEngine()
    let files = ModelFiles(
        ggml: URL(fileURLWithPath: positional[0]),
        coreMLEncoder: options["--coreml"].map { URL(fileURLWithPath: $0) }
    )
    let clock = ContinuousClock()
    let loadTime = try await clock.measure {
        try await engine.loadModel(files, mode: flags.contains("--cpu") ? .cpuOnly : .foregroundAccelerated)
    }
    var segments: [Segment] = []
    let runTime = try await clock.measure {
        segments = try await engine.transcribe(
            samples: samples,
            options: ChunkTranscribeOptions(language: options["--lang"] ?? "ko", prompt: options["--prompt"])
        )
    }
    for segment in segments {
        print(String(format: "[%8.2f → %8.2f] lp=%5.2f ns=%4.2f  %@",
                     from + segment.start, from + segment.end,
                     segment.avgLogprob ?? 0, segment.noSpeechProb ?? 0, segment.text))
    }
    print("load \(loadTime)  transcribe \(runTime) for \(String(format: "%.1f", to - from))s audio  peak footprint \(String(format: "%.0f", peakFootprintMB())) MB")
}

enum CLIError: Error {
    case usage
}

let args = Array(CommandLine.arguments.dropFirst())

switch args.first {
case "info":
    print("whisper.cpp \(WhisperBuildInfo.version)")
    print("backends: \(WhisperBuildInfo.backendNames.joined(separator: ", "))")
    print(WhisperBuildInfo.systemInfo)
case "decode" where args.count >= 2:
    let window = args.count >= 3 ? TimeInterval(args[2]) ?? 30 : 30
    do {
        try await decode(path: args[1], window: window)
    } catch {
        print("error: \(error.localizedDescription)")
        exit(1)
    }
case "transcribe":
    do {
        try await transcribe(arguments: Array(args.dropFirst()))
    } catch CLIError.usage {
        print("usage: sillok-cli transcribe <model.bin> <audio> [--coreml <encoder.mlmodelc>] [--cpu] [--lang ko] [--from s] [--to s] [--prompt text]")
    } catch {
        print("error: \(error)")
        exit(1)
    }
default:
    print("usage: sillok-cli info | decode <file> [windowSeconds] | transcribe ...")
}
