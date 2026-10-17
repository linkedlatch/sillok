using Sillok.Core;

namespace Sillok.Core.Tests;

public class JsonTests
{
    [Fact]
    public void LanguageModesMatchNativeLayout()
    {
        Assert.Equal("""{"auto":{}}""", SillokJson.Serialize(LanguageMode.Auto));
        Assert.Equal("""{"koreanEnglish":{}}""", SillokJson.Serialize(LanguageMode.KoreanEnglish));
        Assert.Equal("""{"fixed":{"_0":"ja"}}""", SillokJson.Serialize(LanguageMode.Fixed("ja")));
        Assert.Equal(LanguageMode.Fixed("ja"), SillokJson.Deserialize<LanguageMode>("""{"fixed":{"_0":"ja"}}"""));
        Assert.Equal(LanguageMode.KoreanEnglish, SillokJson.Deserialize<LanguageMode>("""{"koreanEnglish":{}}"""));
    }

    [Fact]
    public void OptionsCompareByValue()
    {
        var first = new TranscriptionOptions { Vocabulary = ["GOT"], LanguageMode = LanguageMode.Fixed("ko") };
        var second = new TranscriptionOptions { Vocabulary = ["GOT"], LanguageMode = LanguageMode.Fixed("ko") };
        Assert.True(first.SameAs(second));
        Assert.False(first.SameAs(second with { SpeakerDiarization = true }));
    }

    [Fact]
    public void SegmentsOmitMissingValues()
    {
        var json = SillokJson.Serialize(new Segment { Id = "A", Start = 1, End = 2, Text = "안녕", Language = "ko" });
        Assert.Equal("""{"id":"A","start":1,"end":2,"text":"안녕","language":"ko","isLowConfidence":false}""", json);
    }
}

public class WavAudioSourceTests
{
    [Fact]
    public void ReadsSlicesOfPcm16()
    {
        var samples = Enumerable.Range(0, PcmFormat.SampleRate * 2).Select(i => (i % 100) / 200f).ToArray();
        using var source = new WavAudioSource(Wav.WritePcm16(samples));
        Assert.Equal(2.0, source.Duration, 3);
        var slice = source.Read(0.5, 0.75);
        Assert.Equal(PcmFormat.SampleRate / 4, slice.Length);
        Assert.Equal(samples[PcmFormat.SampleRate / 2 + 10], slice[10], 3);
        Assert.Equal(PcmFormat.SampleRate / 2, source.Read(1.5, 9).Length);
        Assert.Empty(source.Read(3, 4));
    }

    [Fact]
    public void RejectsOtherSampleRates()
    {
        var path = Wav.WritePcm16(new float[44100], sampleRate: 44100);
        Assert.Throws<InvalidDataException>(() => new WavAudioSource(path));
    }
}

public class PipelineTests
{
    private static FileTranscriptionPipeline Pipeline(EchoCore core, string checkpoint, TranscriptionOptions? options = null) =>
        new(core, options ?? new TranscriptionOptions { LanguageMode = LanguageMode.Fixed("ko"), Vocabulary = ["GOT"] }, "test", checkpoint);

    private static async Task<List<Segment>> Collect(IAsyncEnumerable<TranscribeProgress> stream, int? stopAfter = null)
    {
        var segments = new List<Segment>();
        var count = 0;
        await foreach (var progress in stream)
        {
            if (progress.ReplacesSegments) segments.Clear();
            segments.AddRange(progress.NewSegments);
            if (stopAfter is { } limit && ++count >= limit) break;
        }
        return segments;
    }

    private static IAudioSource Silence(double seconds) => new ArrayAudioSource(new float[(int)(seconds * PcmFormat.SampleRate)]);

    [Fact]
    public async Task ResumedRunMatchesUninterruptedRun()
    {
        var source = Silence(100);
        var reference = await Collect(Pipeline(new EchoCore(), Repo.TemporaryPath("a.json")).RunAsync(source, TestContext.Current.CancellationToken));
        Assert.Equal(4, reference.Count);

        var checkpoint = Repo.TemporaryPath("b.json");
        var partial = await Collect(Pipeline(new EchoCore(), checkpoint).RunAsync(source, TestContext.Current.CancellationToken), stopAfter: 3);
        Assert.Equal(2, partial.Count);

        var resumedCore = new EchoCore();
        var resumed = await Collect(Pipeline(resumedCore, checkpoint).RunAsync(source, TestContext.Current.CancellationToken));
        Assert.Equal(reference.Select(s => s.Text), resumed.Select(s => s.Text));
        Assert.Equal(2, resumedCore.Steps);
    }

    [Fact]
    public async Task CheckpointWithDifferentOptionsIsIgnored()
    {
        var checkpoint = Repo.TemporaryPath("c.json");
        await Collect(Pipeline(new EchoCore(), checkpoint).RunAsync(Silence(100), TestContext.Current.CancellationToken), stopAfter: 3);
        var core = new EchoCore();
        var segments = await Collect(Pipeline(core, checkpoint, new TranscriptionOptions { Vocabulary = ["other term"] }).RunAsync(Silence(100), TestContext.Current.CancellationToken));
        Assert.Equal(4, segments.Count);
        Assert.Equal(4, core.Steps);
    }

    [Fact]
    public async Task DiarizationReplacesSegmentsAndIsRecorded()
    {
        var core = new EchoCore();
        var checkpoint = Repo.TemporaryPath("d.json");
        var options = new TranscriptionOptions { SpeakerDiarization = true };
        var phases = new List<TranscribePhase>();
        var segments = new List<Segment>();
        await foreach (var progress in Pipeline(core, checkpoint, options).RunAsync(Silence(100), TestContext.Current.CancellationToken))
        {
            phases.Add(progress.Phase);
            if (progress.ReplacesSegments) segments.Clear();
            segments.AddRange(progress.NewSegments);
        }
        Assert.Equal(["1", "2", "1", "2"], segments.Select(s => s.Speaker));
        Assert.Contains(TranscribePhase.Diarizing, phases);
        Assert.Equal(1, core.Diarizations);
        Assert.True(VersionedStore.Load<Checkpoint>(checkpoint).State.Diarized);

        var again = new EchoCore();
        var resumed = await Collect(Pipeline(again, checkpoint, options).RunAsync(Silence(100), TestContext.Current.CancellationToken));
        Assert.Equal(0, again.Steps);
        Assert.Equal(0, again.Diarizations);
        Assert.All(resumed, segment => Assert.NotNull(segment.Speaker));
    }

    [Fact]
    public async Task DiarizationIsSkippedWhenDisabled()
    {
        var core = new EchoCore();
        var segments = await Collect(Pipeline(core, Repo.TemporaryPath("e.json")).RunAsync(Silence(100), TestContext.Current.CancellationToken));
        Assert.All(segments, segment => Assert.Null(segment.Speaker));
        Assert.Equal(0, core.Diarizations);
    }

    [Fact]
    public async Task CancellationStopsBetweenChunks()
    {
        using var cancel = new CancellationTokenSource();
        var core = new EchoCore();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
        {
            await foreach (var progress in Pipeline(core, Repo.TemporaryPath("f.json")).RunAsync(Silence(100), cancel.Token))
            {
                if (progress.Phase == TranscribePhase.Transcribing) cancel.Cancel();
            }
        });
        Assert.Equal(1, core.Steps);
    }
}

public class JobQueueTests
{
    [Fact]
    public async Task RunsJobsInOrderAndDeduplicates()
    {
        var queue = new JobQueue();
        var order = new List<int>();
        var gate = new TaskCompletionSource();
        var idle = new TaskCompletionSource();
        var first = Guid.NewGuid();
        var second = Guid.NewGuid();
        queue.Enqueue(first, async _ =>
        {
            await gate.Task;
            lock (order) order.Add(1);
        });
        queue.Enqueue(second, _ =>
        {
            lock (order) order.Add(2);
            return Task.CompletedTask;
        });
        queue.Enqueue(second, _ =>
        {
            lock (order) order.Add(3);
            return Task.CompletedTask;
        });
        queue.Idle += () => idle.TrySetResult();
        gate.SetResult();
        await idle.Task.WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken);
        Assert.Equal([1, 2], order);
    }

    [Fact]
    public async Task ReenqueueWhileRunningRunsAgain()
    {
        var queue = new JobQueue();
        var runs = 0;
        var gate = new TaskCompletionSource();
        var idle = new TaskCompletionSource();
        var id = Guid.NewGuid();
        Func<CancellationToken, Task> work = async _ =>
        {
            Interlocked.Increment(ref runs);
            await gate.Task;
        };
        queue.Enqueue(id, work);
        await Task.Delay(50, TestContext.Current.CancellationToken);
        queue.Enqueue(id, work);
        queue.Idle += () => idle.TrySetResult();
        gate.SetResult();
        await idle.Task.WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken);
        Assert.Equal(2, runs);
    }

    [Fact]
    public async Task CancelStopsRunningJob()
    {
        var queue = new JobQueue();
        var cancelled = new TaskCompletionSource();
        var id = Guid.NewGuid();
        queue.Enqueue(id, async token =>
        {
            try
            {
                await Task.Delay(Timeout.Infinite, token);
            }
            catch (OperationCanceledException)
            {
                cancelled.SetResult();
            }
        });
        await Task.Delay(50, TestContext.Current.CancellationToken);
        queue.Cancel(id);
        await cancelled.Task.WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken);
    }
}

public class ModelTests
{
    private static ModelManifest Manifest => ModelManifest.Load(Repo.Shared("manifest.json"));

    [Fact]
    public void SharedManifestLoads()
    {
        var manifest = Manifest;
        Assert.Equal(ModelKind.CoreMLEncoder, manifest.Entry("large-v3-turbo-encoder")!.Kind);
        Assert.Equal("large-v3-turbo-encoder", manifest.Entry(ModelSelection.DefaultWhisperId)!.CoreMLEncoderId);
        Assert.Equal(ModelKind.Diarization, manifest.Entry(ModelSelection.DiarizationId)!.Kind);
    }

    [Fact]
    public void RequiredEntriesSkipCoreML()
    {
        Assert.Equal(["large-v3-turbo-q5_0", "silero-v6.2.0"], ModelSelection.RequiredEntries(ModelSelection.DefaultWhisperId, false, Manifest).Select(e => e.Id));
        Assert.Equal(["large-v3-turbo-q5_0", "silero-v6.2.0", "nemotron-3-diarization-q8_0"],
            ModelSelection.RequiredEntries(ModelSelection.DefaultWhisperId, true, Manifest).Select(e => e.Id));
    }

    [Fact]
    public void InstallVerifiesChecksum()
    {
        var root = Repo.TemporaryPath("models");
        var bytes = "model"u8.ToArray();
        var entry = new ModelEntry
        {
            Id = "m", Kind = ModelKind.Vad, DisplayName = "m", FileName = "m.bin", Size = bytes.Length,
            Sha256 = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(bytes)), Urls = [], License = "MIT",
        };
        var store = new ModelStore(root, new ModelManifest(1, [entry]));
        var good = Repo.TemporaryPath("good.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(good)!);
        File.WriteAllBytes(good, bytes);
        store.InstallVerified(good, entry);
        Assert.True(store.IsInstalled(entry));

        var bad = Repo.TemporaryPath("bad.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(bad)!);
        File.WriteAllBytes(bad, "other"u8.ToArray());
        Assert.Throws<ModelChecksumException>(() => store.InstallVerified(bad, entry));
        Assert.False(File.Exists(bad));
    }
}
