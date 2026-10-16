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
default:
    print("usage: sillok-cli info | decode <file> [windowSeconds]")
}
