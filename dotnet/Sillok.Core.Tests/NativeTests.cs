using Sillok.Core;
using Sillok.Core.Native;

namespace Sillok.Core.Tests;

[Collection("native")]
public class NativeTests
{
    private static readonly string Turbo = Repo.Model("ggml-large-v3-turbo-q5_0.bin");
    private static readonly string Vad = Repo.Model("ggml-silero-v6.2.0.bin");
    private static readonly string Diarizer = Repo.Model("Nemotron-3-Diarization.q8_0.gguf");

    private static void RequireFiles(params string[] paths) =>
        Assert.SkipUnless(paths.All(File.Exists), "scripts/fetch-test-models.sh, scripts/make-test-audio.sh를 먼저 실행하세요");

    [Fact]
    public void SystemInfoListsBackends()
    {
        Assert.Contains("whisper.cpp", NativeEngine.SystemInfo);
    }

    [Fact]
    public void ExportUsesNativeFormatter()
    {
        Segment[] segments =
        [
            new() { Start = 0.5, End = 2.25, Text = "안녕하세요", Language = "ko", Speaker = "1" },
            new() { Start = 2.25, End = 3, Text = "네", Language = "ko", Speaker = "2" },
        ];
        Assert.Equal("1\n00:00:00,500 --> 00:00:02,250\n[화자 1] 안녕하세요\n\n2\n00:00:02,250 --> 00:00:03,000\n[화자 2] 네\n",
            NativeExport.Render(segments, ExportFormat.SubRip));
        Assert.Equal("화자 1: 안녕하세요\n화자 2: 네\n", NativeExport.Render(segments, ExportFormat.Text));
    }

    [Fact]
    public async Task MissingModelReportsError()
    {
        using var engine = new NativeEngine();
        await Assert.ThrowsAsync<NativeException>(() => engine.LoadAsync(new ModelFiles("/nonexistent/ggml.bin"), useGpu: true));
    }

    [Fact]
    public async Task PipelineTranscribesEnglish()
    {
        RequireFiles(Turbo, Vad, Repo.Fixture("en.wav"));
        using var engine = new NativeEngine();
        await engine.LoadAsync(new ModelFiles(Turbo), useGpu: true);
        using var source = new WavAudioSource(Repo.Fixture("en.wav"));
        var pipeline = new FileTranscriptionPipeline(
            new NativeCore(engine, Vad), new TranscriptionOptions { LanguageMode = LanguageMode.Fixed("en") }, "turbo", Repo.TemporaryPath("checkpoint.json"));
        var segments = new List<Segment>();
        await foreach (var progress in pipeline.RunAsync(source, TestContext.Current.CancellationToken)) segments.AddRange(progress.NewSegments);
        var text = string.Join(" ", segments.Select(s => s.Text)).ToLowerInvariant();
        Assert.Contains("quick brown fox", text);
        Assert.All(segments, segment => Assert.NotNull(segment.Words));
    }

    [Fact]
    public async Task CancellationStopsTranscription()
    {
        RequireFiles(Turbo, Vad, Repo.Fixture("en-long.wav"));
        using var engine = new NativeEngine();
        await engine.LoadAsync(new ModelFiles(Turbo), useGpu: true);
        using var source = new WavAudioSource(Repo.Fixture("en-long.wav"));
        using var cancel = new CancellationTokenSource();
        var core = new NativeCore(engine, Vad);
        var chunks = await core.PlanAsync(source, CancellationToken.None);
        cancel.CancelAfter(TimeSpan.FromMilliseconds(300));
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            core.StepAsync(new TranscriptionState(), chunks, source, new TranscriptionOptions(), cancel.Token));
    }

    [Fact]
    public async Task DiarizationLabelsThreeSpeakers()
    {
        var parts = Enumerable.Range(1, 7).Select(i => Repo.Fixture($"dialog-{i}.wav")).ToArray();
        RequireFiles([Turbo, Vad, Diarizer, .. parts]);
        var samples = new List<float>();
        var middles = new List<double>();
        foreach (var part in parts)
        {
            using var source = new WavAudioSource(part);
            middles.Add(samples.Count / (double)PcmFormat.SampleRate + source.Duration / 2);
            samples.AddRange(source.Read(0, source.Duration));
            samples.AddRange(new float[PcmFormat.SampleRate / 2]);
        }
        using var engine = new NativeEngine();
        await engine.LoadAsync(new ModelFiles(Turbo), useGpu: true);
        var pipeline = new FileTranscriptionPipeline(
            new NativeCore(engine, Vad, Diarizer),
            new TranscriptionOptions { LanguageMode = LanguageMode.Fixed("ko"), SpeakerDiarization = true },
            "turbo",
            Repo.TemporaryPath("checkpoint.json"));
        var segments = new List<Segment>();
        await foreach (var progress in pipeline.RunAsync(new ArrayAudioSource([.. samples]), TestContext.Current.CancellationToken))
        {
            if (progress.ReplacesSegments) segments.Clear();
            segments.AddRange(progress.NewSegments);
        }
        Assert.Null(engine.LoadedFiles);
        static double Distance(Segment segment, double time) => Math.Max(Math.Max(segment.Start - time, time - segment.End), 0);
        var speakers = middles.Select(time => segments.MinBy(segment => Distance(segment, time))!.Speaker).ToArray();
        Assert.All(speakers, Assert.NotNull);
        Assert.Equal(3, speakers.Distinct().Count());
        Assert.Equal(speakers[0], speakers[2]);
        Assert.Equal(speakers[0], speakers[6]);
        Assert.Equal(speakers[1], speakers[4]);
        Assert.Equal(speakers[3], speakers[5]);
    }
}
