import Darwin
import Foundation
import SillokAudio
import SillokCore
import SillokNative
import SillokPlatform

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

func seconds(_ duration: Duration) -> Double {
    Double(duration.components.seconds) + Double(duration.components.attoseconds) / 1e18
}

struct Arguments {
    var positional: [String] = []
    var options: [String: String] = [:]
    var flags: Set<String> = []

    init(_ arguments: ArraySlice<String>, flags known: Set<String> = ["--cpu"]) {
        var iterator = arguments.makeIterator()
        while let argument = iterator.next() {
            if known.contains(argument) {
                flags.insert(argument)
            } else if argument.hasPrefix("--"), let value = iterator.next() {
                options[argument] = value
            } else {
                positional.append(argument)
            }
        }
    }

    func double(_ name: String) -> Double? { options[name].flatMap(Double.init) }
}

enum CLIError: Error {
    case usage(String)
}

func languageMode(_ value: String?) -> LanguageMode {
    switch value {
    case nil, "auto": .auto
    case "koen": .koreanEnglish
    case let code?: .fixed(code)
    }
}

func loadEngine(model: String, coreML: String?, cpu: Bool) async throws -> (NativeEngine, Double) {
    let engine = NativeEngine()
    let clock = ContinuousClock()
    let elapsed = try await clock.measure {
        try await engine.load(
            ModelFiles(ggml: URL(fileURLWithPath: model), coreMLEncoder: coreML.map { URL(fileURLWithPath: $0) }),
            mode: cpu ? .cpuOnly : .foregroundAccelerated
        )
    }
    return (engine, seconds(elapsed))
}

func decode(_ args: Arguments) async throws {
    guard let path = args.positional.first else { throw CLIError.usage("decode <file> [--window seconds]") }
    let window = args.double("--window") ?? 30
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: path))
    let clock = ContinuousClock()
    var total = 0
    let elapsed = try await clock.measure {
        var start: TimeInterval = 0
        while start < source.duration {
            total += try await source.readAsync(from: start, to: start + window).count
            start += window
        }
    }
    print(String(format: "duration %.3fs  samples %d (%.3fs)  window %.0fs", source.duration, total, Double(total) / Double(PCMFormat.sampleRate), window))
    print(String(format: "elapsed %.2fs (%.0fx realtime)  peak footprint %.1f MB", seconds(elapsed), source.duration / seconds(elapsed), peakFootprintMB()))
}

func plan(_ args: Arguments) async throws {
    guard args.positional.count == 2 else { throw CLIError.usage("plan <audio> <vad.bin>") }
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: args.positional[0]))
    let core = NativeCore(engine: NativeEngine(), vadModel: URL(fileURLWithPath: args.positional[1]))
    let clock = ContinuousClock()
    var chunks: [AudioChunk] = []
    let elapsed = try await clock.measure { chunks = try await core.plan(source: source) }
    let lengths = chunks.map(\.duration).sorted()
    print(String(format: "duration %.1fs  vad %.1fs  chunks %d  forced cuts %d", source.duration, seconds(elapsed), chunks.count, chunks.filter { $0.cutAfter != nil }.count))
    if !lengths.isEmpty {
        print(String(format: "chunk length min %.1fs  median %.1fs  max %.1fs", lengths.first!, lengths[lengths.count / 2], lengths.last!))
    }
    for chunk in chunks.prefix(12) {
        print(String(format: "  %8.2f → %8.2f  (%.1fs)%@%@", chunk.start, chunk.end, chunk.duration,
                     chunk.cutBefore.map { String(format: "  cutBefore %.2f", $0) } ?? "",
                     chunk.cutAfter.map { String(format: "  cutAfter %.2f", $0) } ?? ""))
    }
}

func transcribe(_ args: Arguments) async throws {
    guard args.positional.count == 2 else {
        throw CLIError.usage("transcribe <model.bin> <audio> [--coreml enc] [--cpu] [--lang ko] [--from s] [--to s] [--prompt text]")
    }
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: args.positional[1]))
    let from = args.double("--from") ?? 0
    let to = args.double("--to") ?? min(source.duration, from + 30)
    let samples = try await source.readAsync(from: from, to: to)
    let (engine, loadTime) = try await loadEngine(model: args.positional[0], coreML: args.options["--coreml"], cpu: args.flags.contains("--cpu"))

    let clock = ContinuousClock()
    var segments: [Segment] = []
    let elapsed = try await clock.measure {
        segments = try await engine.transcribe(samples: samples, options: ChunkTranscribeOptions(language: args.options["--lang"] ?? "ko", prompt: args.options["--prompt"]))
    }
    for segment in segments {
        print(String(format: "[%8.2f → %8.2f] lp=%5.2f ns=%4.2f  %@", from + segment.start, from + segment.end,
                     segment.avgLogprob ?? 0, segment.noSpeechProb ?? 0, segment.text))
    }
    print(String(format: "load %.2fs  transcribe %.2fs for %.1fs audio  peak footprint %.0f MB", loadTime, seconds(elapsed), to - from, peakFootprintMB()))
}

func languages(_ args: Arguments) async throws {
    guard args.positional.count == 3 else { throw CLIError.usage("langs <audio> <vad.bin> <model.bin> [--coreml enc]") }
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: args.positional[0]))
    let (engine, _) = try await loadEngine(model: args.positional[2], coreML: args.options["--coreml"], cpu: false)
    let chunks = try await NativeCore(engine: engine, vadModel: URL(fileURLWithPath: args.positional[1])).plan(source: source)
    for (index, chunk) in chunks.enumerated() {
        let samples = try await source.readAsync(from: chunk.start, to: chunk.end)
        let any = try await engine.detectLanguage(samples: samples, allowedLanguages: nil)
        let koen = try await engine.detectLanguage(samples: samples, allowedLanguages: ["ko", "en"])
        print(String(format: "chunk %3d  %7.1f-%7.1f  auto %@ %.2f  koen %@ %.2f", index + 1, chunk.start, chunk.end,
                     any.language, any.confidence, koen.language, koen.confidence))
        fflush(stdout)
    }
}

func run(_ args: Arguments) async throws {
    guard args.positional.count == 3 else {
        throw CLIError.usage("run <audio> <vad.bin> <model.bin> [--coreml enc] [--lang auto|koen|<code>] [--terms a,b] [--out file] [--checkpoint file]")
    }
    let clock = ContinuousClock()
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: args.positional[0]))
    let (engine, loadTime) = try await loadEngine(model: args.positional[2], coreML: args.options["--coreml"], cpu: false)
    print(String(format: "audio %.1fs  load %.1fs", source.duration, loadTime))

    let checkpointURL = args.options["--checkpoint"].map { URL(fileURLWithPath: $0) }
        ?? FileManager.default.temporaryDirectory.appendingPathComponent("sillok-cli-\(UUID().uuidString).json")
    var pipeline = FileTranscriptionPipeline(
        core: NativeCore(engine: engine, vadModel: URL(fileURLWithPath: args.positional[1])),
        options: TranscriptionOptions(
            languageMode: languageMode(args.options["--lang"]),
            vocabulary: args.options["--terms"]?.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) } ?? []
        ),
        modelID: URL(fileURLWithPath: args.positional[2]).lastPathComponent,
        checkpointURL: checkpointURL
    )
    pipeline.thermal = ThermalMonitor()
    let activity = ActivityAssertion(reason: "transcribing")

    var segments: [Segment] = []
    let started = clock.now
    for try await progress in pipeline.run(source: source) {
        segments += progress.newSegments
        print(String(format: "%@ %5.1f%%  at %7.1fs  +%d segs  elapsed %6.1fs",
                     progress.phase == .analyzing ? "analyzing  " : "transcribing", progress.fractionCompleted * 100,
                     progress.lastProcessedTimestamp, progress.newSegments.count, seconds(clock.now - started)))
        fflush(stdout)
    }
    withExtendedLifetime(activity) {}
    try? FileManager.default.removeItem(at: checkpointURL)

    let total = seconds(clock.now - started)
    let languages = Dictionary(grouping: segments, by: \.language).mapValues(\.count)
    print(String(format: "TOTAL %.1fs for %.1fs audio → %.1fx realtime", total, source.duration, source.duration / total))
    print(String(format: "peak footprint %.0f MB  segments %d  low-confidence %d  languages %@",
                 peakFootprintMB(), segments.count, segments.filter(\.isLowConfidence).count, languages.description))

    if let output = args.options["--out"] {
        let text = segments.map {
            String(format: "[%7.2f → %7.2f] %@%@ ", $0.start, $0.end, $0.language, $0.isLowConfidence ? " ?" : "") + $0.text
        }.joined(separator: "\n")
        try text.write(toFile: output, atomically: true, encoding: .utf8)
        print("wrote \(output)")
    }
}

let command = CommandLine.arguments.dropFirst().first
let arguments = Arguments(CommandLine.arguments.dropFirst(2))

do {
    switch command {
    case "info": print(NativeEngine.systemInfo)
    case "decode": try await decode(arguments)
    case "plan": try await plan(arguments)
    case "transcribe": try await transcribe(arguments)
    case "langs": try await languages(arguments)
    case "run": try await run(arguments)
    default: print("usage: sillok-cli info | decode | plan | transcribe | langs | run")
    }
} catch CLIError.usage(let usage) {
    print("usage: sillok-cli \(usage)")
    exit(2)
} catch {
    print("error: \(error.localizedDescription)")
    exit(1)
}
