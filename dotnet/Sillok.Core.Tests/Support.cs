global using Xunit;
using Sillok.Core;

namespace Sillok.Core.Tests;

internal static class Repo
{
    public static readonly string Root = FindRoot();

    public static string Model(string name) => Path.Combine(Root, "models", name);

    public static string Fixture(string name) => Path.Combine(Root, "build", "fixtures", name);

    public static string Shared(string name) => Path.Combine(Root, "shared", name);

    public static string TemporaryPath(string name) => Path.Combine(Path.GetTempPath(), $"sillok-{Guid.NewGuid():N}", name);

    private static string FindRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !File.Exists(Path.Combine(directory.FullName, "shared", "manifest.json")))
        {
            directory = directory.Parent;
        }
        return directory?.FullName ?? throw new InvalidOperationException("저장소 루트를 찾지 못했습니다");
    }
}

internal sealed class EchoCore : ITranscriptionCore
{
    public int Steps { get; private set; }
    public int Diarizations { get; private set; }

    public Task<IReadOnlyList<AudioChunk>> PlanAsync(IAudioSource source, CancellationToken cancellation)
    {
        var chunks = new List<AudioChunk>();
        for (double start = 0; start < source.Duration; start += 25) chunks.Add(new AudioChunk(start, Math.Min(start + 25, source.Duration)));
        return Task.FromResult<IReadOnlyList<AudioChunk>>(chunks);
    }

    public Task<TranscriptionState> StepAsync(
        TranscriptionState state, IReadOnlyList<AudioChunk> chunks, IAudioSource source, TranscriptionOptions options, CancellationToken cancellation)
    {
        Steps++;
        var chunk = chunks[state.NextChunk];
        var text = $"p[{state.PreviousText}] n{state.NextChunk}";
        return Task.FromResult(state with
        {
            NextChunk = state.NextChunk + 1,
            PreviousText = text,
            Segments = [.. state.Segments, new Segment { Start = chunk.Start, End = chunk.End, Text = text, Language = "ko" }],
        });
    }

    public Task<IReadOnlyList<Segment>> DiarizeAsync(IReadOnlyList<Segment> segments, IAudioSource source, CancellationToken cancellation)
    {
        Diarizations++;
        return Task.FromResult<IReadOnlyList<Segment>>(segments.Select((segment, index) => segment with { Speaker = (index % 2 + 1).ToString() }).ToList());
    }
}

internal static class Wav
{
    public static string WritePcm16(float[] samples, int sampleRate = PcmFormat.SampleRate, int channels = 1)
    {
        var path = Repo.TemporaryPath("audio.wav");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var writer = new BinaryWriter(File.Create(path));
        writer.Write("RIFF"u8);
        writer.Write(36 + samples.Length * 2);
        writer.Write("WAVE"u8);
        writer.Write("fmt "u8);
        writer.Write(16);
        writer.Write((short)1);
        writer.Write((short)channels);
        writer.Write(sampleRate);
        writer.Write(sampleRate * channels * 2);
        writer.Write((short)(channels * 2));
        writer.Write((short)16);
        writer.Write("data"u8);
        writer.Write(samples.Length * 2);
        foreach (var sample in samples) writer.Write((short)Math.Round(sample * 32767));
        return path;
    }
}
