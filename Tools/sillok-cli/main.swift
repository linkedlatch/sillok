import Darwin
import Foundation
import SillokAudio
import SillokCore
import SillokPlatform
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

func plan(audio: String, vadModel: String, model: String?, chunkRange: Range<Int>) async throws {
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: audio))
    let vad = try WhisperVAD(modelURL: URL(fileURLWithPath: vadModel))
    let clock = ContinuousClock()
    var map: SpeechMap?
    let elapsed = try await clock.measure { map = try await SpeechMap.build(source: source, vad: vad) }
    guard let map else { return }
    let chunks = ChunkPlanner().plan(map)
    let covered = chunks.reduce(0) { $0 + $1.duration }
    let forced = chunks.filter { $0.cutAfter != nil }.count
    let lengths = chunks.map(\.duration).sorted()
    print(String(format: "duration %.1fs  vad %.1fs  frames %d", map.duration, Double(elapsed.components.seconds) + Double(elapsed.components.attoseconds) / 1e18, map.probabilities.count))
    print(String(format: "chunks %d  forced cuts %d  covered %.1fs (%.0f%%)", chunks.count, forced, covered, covered / map.duration * 100))
    if !lengths.isEmpty {
        print(String(format: "chunk length min %.1fs  median %.1fs  max %.1fs", lengths.first!, lengths[lengths.count / 2], lengths.last!))
    }
    guard let model else {
        for chunk in chunks.prefix(12) { printChunk(chunk) }
        return
    }
    let engine = WhisperCppEngine()
    try await engine.loadModel(ModelFiles(ggml: URL(fileURLWithPath: model)), mode: .foregroundAccelerated)
    for chunk in chunks[chunkRange.clamped(to: 0..<chunks.count)] {
        printChunk(chunk)
        let samples = try await source.read(from: chunk.start, to: chunk.end)
        let raw = try await engine.transcribe(samples: samples, options: ChunkTranscribeOptions(language: "ko"))
            .map { $0.offset(by: chunk.start) }
        let kept = OverlapMerger.trim(raw, to: chunk)
        for segment in raw where chunk.cutBefore.map({ segment.start < $0 + 1 }) ?? false || chunk.cutAfter.map({ segment.end > $0 - 1 }) ?? false {
            print("    raw  " + String(format: "[%7.2f → %7.2f] ", segment.start, segment.end) + segment.text)
        }
        for segment in kept {
            print("    kept " + String(format: "[%7.2f → %7.2f] ", segment.start, segment.end) + segment.text)
        }
    }
}

func printChunk(_ chunk: AudioChunk) {
    print(String(format: "  %8.2f → %8.2f  (%.1fs)%@%@", chunk.start, chunk.end, chunk.duration,
                 chunk.cutBefore.map { String(format: "  cutBefore %.2f", $0) } ?? "",
                 chunk.cutAfter.map { String(format: "  cutAfter %.2f", $0) } ?? ""))
}

func runFile(audio: String, vadModel: String, model: String, coreML: String?, mode: LanguageMode, terms: [String], output: String?, checkpoint: String?) async throws {
    let clock = ContinuousClock()
    func seconds(_ d: Duration) -> Double { Double(d.components.seconds) + Double(d.components.attoseconds) / 1e18 }

    let source = try await AssetAudioSource(url: URL(fileURLWithPath: audio))
    let engine = WhisperCppEngine()
    let modelURL = URL(fileURLWithPath: model)
    let loadTime = try await clock.measure {
        try await engine.loadModel(ModelFiles(ggml: modelURL, coreMLEncoder: coreML.map { URL(fileURLWithPath: $0) }), mode: .foregroundAccelerated)
    }
    print(String(format: "audio %.1fs  load %.1fs", source.duration, seconds(loadTime)))

    let checkpointURL = checkpoint.map { URL(fileURLWithPath: $0) }
        ?? FileManager.default.temporaryDirectory.appendingPathComponent("sillok-cli-\(UUID().uuidString).json")
    var pipeline = FileTranscriptionPipeline(
        engine: engine,
        vad: try WhisperVAD(modelURL: URL(fileURLWithPath: vadModel)),
        options: TranscriptionOptions(languageMode: mode, vocabulary: terms),
        modelID: modelURL.lastPathComponent,
        checkpointURL: checkpointURL
    )
    pipeline.thermal = ThermalMonitor()
    let activity = ActivityAssertion(reason: "transcribing")

    var segments: [Segment] = []
    var walls: [Double] = []
    var last = clock.now
    let started = clock.now
    for try await progress in pipeline.run(source: source) {
        let now = clock.now
        segments += progress.newSegments
        if progress.phase == .transcribing, progress.fractionCompleted < 1, !progress.newSegments.isEmpty || walls.count > 0 {
            walls.append(seconds(now - last))
        }
        last = now
        print(String(format: "%@ %5.1f%%  at %7.1fs  +%d segs  elapsed %6.1fs",
                     progress.phase == .analyzing ? "analyzing  " : "transcribing", progress.fractionCompleted * 100,
                     progress.lastProcessedTimestamp, progress.newSegments.count, seconds(now - started)))
        fflush(stdout)
    }
    withExtendedLifetime(activity) {}
    try? FileManager.default.removeItem(at: checkpointURL)

    let total = seconds(clock.now - started)
    let languages = Dictionary(grouping: segments, by: \.language).mapValues(\.count)
    print(String(format: "TOTAL %.1fs for %.1fs audio → %.1fx realtime", total, source.duration, source.duration / total))
    print(String(format: "peak footprint %.0f MB  segments %d  low-confidence %d  languages %@",
                 peakFootprintMB(), segments.count, segments.filter(\.isLowConfidence).count, languages.description))

    if let output {
        let text = segments.map {
            String(format: "[%7.2f → %7.2f] %@%@ ", $0.start, $0.end, $0.language, $0.isLowConfidence ? " ?" : "") + $0.text
        }.joined(separator: "\n")
        try text.write(toFile: output, atomically: true, encoding: .utf8)
        print("wrote \(output)")
    }
}

func detectLanguages(audio: String, vadModel: String, model: String, coreML: String?) async throws {
    let source = try await AssetAudioSource(url: URL(fileURLWithPath: audio))
    let map = try await SpeechMap.build(source: source, vad: try WhisperVAD(modelURL: URL(fileURLWithPath: vadModel)))
    let chunks = ChunkPlanner().plan(map)
    let engine = WhisperCppEngine()
    try await engine.loadModel(
        ModelFiles(ggml: URL(fileURLWithPath: model), coreMLEncoder: coreML.map { URL(fileURLWithPath: $0) }),
        mode: .foregroundAccelerated
    )
    for (index, chunk) in chunks.enumerated() {
        let samples = try await source.read(from: chunk.start, to: chunk.end)
        let any = try await engine.detectLanguage(samples: samples, allowedLanguages: nil)
        let koen = try await engine.detectLanguage(samples: samples, allowedLanguages: ["ko", "en"])
        print(String(format: "chunk %3d  %7.1f-%7.1f  auto %@ %.2f  koen %@ %.2f", index + 1, chunk.start, chunk.end,
                     any.language, any.confidence, koen.language, koen.confidence))
        fflush(stdout)
    }
}

func languageMode(_ value: String?) -> LanguageMode {
    switch value {
    case nil, "auto": .auto
    case "koen": .koreanEnglish
    case let code?: .fixed(code)
    }
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
case "plan" where args.count >= 3:
    do {
        let model = args.firstIndex(of: "--model").flatMap { args.indices.contains($0 + 1) ? args[$0 + 1] : nil }
        let from = args.firstIndex(of: "--chunks").flatMap { args.indices.contains($0 + 1) ? Int(args[$0 + 1]) : nil } ?? 0
        let count = args.firstIndex(of: "--count").flatMap { args.indices.contains($0 + 1) ? Int(args[$0 + 1]) : nil } ?? 2
        try await plan(audio: args[1], vadModel: args[2], model: model, chunkRange: from..<(from + count))
    } catch {
        print("error: \(error)")
        exit(1)
    }
case "run" where args.count >= 4:
    func value(_ flag: String) -> String? {
        args.firstIndex(of: flag).flatMap { args.indices.contains($0 + 1) ? args[$0 + 1] : nil }
    }
    do {
        try await runFile(audio: args[1], vadModel: args[2], model: args[3], coreML: value("--coreml"),
                          mode: languageMode(value("--lang")),
                          terms: value("--terms")?.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) } ?? [],
                          output: value("--out"), checkpoint: value("--checkpoint"))
    } catch {
        print("error: \(error)")
        exit(1)
    }
case "langs" where args.count >= 4:
    do {
        let coreML = args.firstIndex(of: "--coreml").flatMap { args.indices.contains($0 + 1) ? args[$0 + 1] : nil }
        try await detectLanguages(audio: args[1], vadModel: args[2], model: args[3], coreML: coreML)
    } catch {
        print("error: \(error)")
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
    print("usage: sillok-cli info | decode <file> [windowSeconds] | plan <audio> <vad.bin> | run <audio> <vad.bin> <model.bin> [--coreml enc] [--lang auto|koen|<code>] [--terms a,b] [--out file] [--checkpoint file] | transcribe ...")
}
