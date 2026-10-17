using System.Diagnostics;
using Sillok.Core;
using Sillok.Core.Native;

namespace Sillok.Cli;

internal static class Program
{
    private const string Usage = """
        사용법:
          sillok transcribe <오디오> [--lang auto|koen|<코드>] [--terms 용어1,용어2] [--diarize]
                                    [--format txt|srt|json] [--out 파일] [--checkpoint 파일] [--cpu]
          sillok models [download] [--diarize]
          sillok info

        모델 위치는 --models <폴더> 또는 SILLOK_MODELS 환경 변수로 바꿀 수 있습니다.
        """;

    private static async Task<int> Main(string[] args)
    {
        using var cancel = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            cancel.Cancel();
        };
        var arguments = Arguments.Parse(args, flags: new HashSet<string> { "--diarize", "--cpu" });
        try
        {
            return arguments.Positional.FirstOrDefault() switch
            {
                "transcribe" => await Transcribe(arguments, cancel.Token),
                "models" => await Models(arguments, cancel.Token),
                "info" => Info(arguments),
                _ => Fail(Usage),
            };
        }
        catch (OperationCanceledException)
        {
            Console.Error.WriteLine("취소했습니다. 같은 --checkpoint로 다시 실행하면 이어서 전사합니다");
            return 130;
        }
        catch (Exception error) when (error is NativeException or IOException or InvalidDataException or FfmpegNotFoundException
                                          or ModelChecksumException or InsufficientDiskSpaceException or HttpRequestException)
        {
            return Fail(error.Message);
        }
    }

    private static async Task<int> Transcribe(Arguments arguments, CancellationToken cancellation)
    {
        if (arguments.Positional.Count != 2) return Fail(Usage);
        var input = Path.GetFullPath(arguments.Positional[1]);
        var diarize = arguments.Has("--diarize");
        var store = ModelStoreFor(arguments);
        var missing = ModelSelection.RequiredEntries(ModelSelection.DefaultWhisperId, diarize, store.Manifest).Where(entry => !store.IsInstalled(entry)).ToList();
        if (missing.Count > 0)
        {
            return Fail("모델이 없습니다: " + string.Join(", ", missing.Select(entry => entry.DisplayName)) +
                        $"\n`sillok models download{(diarize ? " --diarize" : "")}`로 받아 주세요");
        }

        var temporary = Path.Combine(Path.GetTempPath(), $"sillok-{Guid.NewGuid():N}");
        Directory.CreateDirectory(temporary);
        try
        {
            using var source = await OpenAudio(input, temporary, cancellation);
            using var engine = new NativeEngine();
            var whisper = store.Manifest.Entry(ModelSelection.DefaultWhisperId)!;
            var clock = Stopwatch.StartNew();
            await engine.LoadAsync(new ModelFiles(store.Location(whisper)), useGpu: !arguments.Has("--cpu"));
            Console.Error.WriteLine($"오디오 {source.Duration:F1}초, 모델 로드 {clock.Elapsed.TotalSeconds:F1}초");

            var options = new TranscriptionOptions
            {
                LanguageMode = ParseLanguage(arguments.Value("--lang")),
                Vocabulary = arguments.Value("--terms")?.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries) ?? [],
                SpeakerDiarization = diarize,
            };
            var diarizer = diarize ? store.Location(store.Manifest.Entry(ModelSelection.DiarizationId)!) : null;
            var checkpoint = arguments.Value("--checkpoint") ?? Path.Combine(temporary, "checkpoint.json");
            var pipeline = new FileTranscriptionPipeline(
                new NativeCore(engine, store.Location(store.Manifest.Entry(ModelSelection.VadId)!), diarizer), options, whisper.Id, checkpoint);

            var segments = new List<Segment>();
            clock.Restart();
            await foreach (var progress in pipeline.RunAsync(source, cancellation))
            {
                if (progress.ReplacesSegments) segments.Clear();
                segments.AddRange(progress.NewSegments);
                Console.Error.Write($"\r{PhaseText(progress)} {progress.FractionCompleted * 100,5:F1}%  {clock.Elapsed.TotalSeconds,6:F1}초   ");
            }
            Console.Error.WriteLine();
            Console.Error.WriteLine($"{clock.Elapsed.TotalSeconds:F1}초 동안 {source.Duration:F1}초 처리 ({source.Duration / clock.Elapsed.TotalSeconds:F1}배 실시간), 세그먼트 {segments.Count}개");
            if (arguments.Value("--checkpoint") is { } saved) File.Delete(saved);

            var output = (arguments.Value("--format") ?? "txt") switch
            {
                "srt" => NativeExport.Render(segments, ExportFormat.SubRip),
                "json" => SillokJson.Serialize(segments),
                "txt" => NativeExport.Render(segments, ExportFormat.Text),
                var other => throw new InvalidDataException($"알 수 없는 형식입니다: {other}"),
            };
            if (arguments.Value("--out") is { } path)
            {
                await File.WriteAllTextAsync(path, output, cancellation);
                Console.Error.WriteLine($"저장했습니다: {path}");
            }
            else
            {
                Console.Out.Write(output);
            }
            return 0;
        }
        finally
        {
            Directory.Delete(temporary, recursive: true);
        }
    }

    private static async Task<int> Models(Arguments arguments, CancellationToken cancellation)
    {
        var store = ModelStoreFor(arguments);
        var required = ModelSelection.RequiredEntries(ModelSelection.DefaultWhisperId, arguments.Has("--diarize"), store.Manifest);
        if (arguments.Positional.ElementAtOrDefault(1) == "download")
        {
            using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
            var downloader = new HttpModelDownloader(store, client);
            foreach (var entry in required.Where(entry => !store.IsInstalled(entry)))
            {
                var progress = new Progress<long>(received =>
                    Console.Error.Write($"\r{entry.DisplayName} {received / 1_048_576}/{entry.Size / 1_048_576}MB   "));
                await downloader.DownloadAsync(entry, progress, cancellation);
                Console.Error.WriteLine();
            }
        }
        Console.WriteLine($"모델 폴더: {ModelsDirectory(arguments)}");
        foreach (var entry in store.Manifest.Models.Where(entry => entry.Kind != ModelKind.CoreMLEncoder))
        {
            var state = store.IsInstalled(entry) ? "설치됨" : required.Contains(entry) ? "필요" : "선택";
            Console.WriteLine($"  [{state}] {entry.DisplayName} ({entry.Size / 1_048_576}MB, {entry.License})");
        }
        return 0;
    }

    private static int Info(Arguments arguments)
    {
        Console.WriteLine(NativeEngine.SystemInfo);
        Console.WriteLine($"모델 폴더: {ModelsDirectory(arguments)}");
        return 0;
    }

    private static async Task<WavAudioSource> OpenAudio(string input, string temporary, CancellationToken cancellation)
    {
        if (!File.Exists(input)) throw new IOException($"파일이 없습니다: {input}");
        try
        {
            return new WavAudioSource(input);
        }
        catch (InvalidDataException)
        {
            var converted = Path.Combine(temporary, "audio.wav");
            Console.Error.WriteLine("ffmpeg로 16kHz 모노 WAV로 변환하는 중");
            await FfmpegConverter.ConvertAsync(input, converted, cancellation);
            return new WavAudioSource(converted);
        }
    }

    private static LanguageMode ParseLanguage(string? value) => value switch
    {
        null or "auto" => LanguageMode.Auto,
        "koen" => LanguageMode.KoreanEnglish,
        var code => LanguageMode.Fixed(code),
    };

    private static string PhaseText(TranscribeProgress progress) => progress.Phase switch
    {
        TranscribePhase.Analyzing => "음성 구간 분석",
        TranscribePhase.Transcribing => "전사",
        _ => "화자 구분",
    };

    private static string ModelsDirectory(Arguments arguments) =>
        arguments.Value("--models")
        ?? Environment.GetEnvironmentVariable("SILLOK_MODELS")
        ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Sillok", "Models");

    private static ModelStore ModelStoreFor(Arguments arguments) =>
        new(ModelsDirectory(arguments), ModelManifest.Load(Path.Combine(AppContext.BaseDirectory, "manifest.json")));

    private static int Fail(string message)
    {
        Console.Error.WriteLine(message);
        return 1;
    }
}

internal sealed record Arguments(IReadOnlyList<string> Positional, IReadOnlyDictionary<string, string> Options, IReadOnlySet<string> Flags)
{
    public static Arguments Parse(string[] args, IReadOnlySet<string> flags)
    {
        var positional = new List<string>();
        var options = new Dictionary<string, string>();
        var present = new HashSet<string>();
        for (var i = 0; i < args.Length; i++)
        {
            if (flags.Contains(args[i]))
            {
                present.Add(args[i]);
            }
            else if (args[i].StartsWith("--") && i + 1 < args.Length)
            {
                options[args[i]] = args[++i];
            }
            else
            {
                positional.Add(args[i]);
            }
        }
        return new Arguments(positional, options, present);
    }

    public string? Value(string name) => Options.GetValueOrDefault(name);

    public bool Has(string flag) => Flags.Contains(flag);
}
