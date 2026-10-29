using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Sillok;

/// <summary>A file of a catalog model; <see cref="Zip"/> files are optional accelerators (Apple only).</summary>
public sealed record ModelFile(string File, string Url, long Size, string Sha256, bool Zip);

/// <summary>
/// A model of the catalog. <see cref="Role"/> is "voice_detection",
/// "transcription" or "diarization"; <see cref="Family"/> and
/// <see cref="Quantization"/> (e.g. "large-v3-turbo", "q5_0") describe it.
/// </summary>
public sealed record CatalogModel(string Id, string Role, string Family, string Quantization, bool Recommended, IReadOnlyList<ModelFile> Files)
{
    /// <summary>Bytes to download: the files the app uses, not the archives.</summary>
    public long Size => Files.Where(f => !f.Zip).Sum(f => f.Size);
}

/// <summary>A compute device the library can use; <see cref="Type"/> is "cpu", "gpu", "igpu", ...</summary>
public sealed record ComputeDevice(string Name, string Description, string Type, long MemoryTotal)
{
    public bool IsGpu => Type is "gpu" or "igpu";
}

/// <summary>The processing stages a run reports, in order.</summary>
public enum Stage { Analyze = 0, Transcribe = 1, Diarize = 2 }

/// <summary>The library-wide functions of the C ABI.</summary>
public static class SillokLibrary
{
    /// <summary>Samples per second of a job's recording (SILLOK_SAMPLE_RATE).</summary>
    public const int SampleRate = 16_000;

    public static string Version => Marshal.PtrToStringUTF8(Native.Version()) ?? "";

    public static IReadOnlyList<CatalogModel> Catalog()
    {
        Native.Check(Native.ModelCatalog(out var json));
        return Objects(Native.Take(json)).Select(o => new CatalogModel(
            (string)o["id"]!, (string)o["role"]!, (string)o["family"]!, (string)o["quantization"]!, (bool)o["recommended"]!,
            o["files"]!.AsArray().Select(f => new ModelFile(
                (string)f!["file"]!, (string)f["url"]!, (long)f["size"]!, (string)f["sha256"]!, (bool)f["zip"]!)).ToList())).ToList();
    }

    /// <summary>The models an engine uses: each role's recommended one, with <paramref name="transcription"/> (when given) for transcription.</summary>
    public static IReadOnlyList<CatalogModel> EngineModels(string? transcription) =>
        Catalog().Where(m => m.Role == "transcription" && transcription is not null ? m.Id == transcription : m.Recommended).ToList();

    /// <summary>Status of each catalog file in <paramref name="dir"/> by file name: "ok", "missing", "wrong_size", ...</summary>
    public static IReadOnlyDictionary<string, string> Check(string dir, bool verifyHash)
    {
        Native.Check(Native.ModelCheck(dir, verifyHash ? 1 : 0, out var json));
        // Files shared between models appear once per model.
        return Objects(Native.Take(json))
            .GroupBy(o => (string)o["file"]!)
            .ToDictionary(g => g.Key, g => (string)g.First()["status"]!);
    }

    public static IReadOnlyList<ComputeDevice> Devices()
    {
        Native.Check(Native.Devices(out var json));
        return Objects(Native.Take(json)).Select(o => new ComputeDevice(
            (string)o["name"]!, (string)o["description"]!, (string)o["type"]!, (long)o["memory_total"]!)).ToList();
    }

    /// <summary>Renders a transcript document as "txt", "srt" or "json"; unnamed speakers get <paramref name="speakerLabel"/> ("{n}" is the number).</summary>
    public static string Export(string transcript, string format, string speakerLabel)
    {
        var options = new JsonObject { ["speaker_label"] = speakerLabel }.ToJsonString();
        Native.Check(Native.Export(transcript, format, options, out var text));
        return Native.Take(text);
    }

    private static IEnumerable<JsonObject> Objects(string json) =>
        JsonNode.Parse(json)!.AsArray().Select(n => n!.AsObject());
}

/// <summary>sillok_engine: loads the models in a directory while a job runs.</summary>
public sealed class Engine : IDisposable
{
    internal nint Handle { get; private set; }

    /// <param name="gpuIndex">null for the CPU only.</param>
    /// <param name="threads">0 for the library's default.</param>
    /// <param name="transcriptionModel">A catalog id; null for the recommended model.</param>
    public Engine(string modelsDir, string tempDir, int? gpuIndex, int threads = 0, string? transcriptionModel = null)
    {
        var config = new JsonObject
        {
            ["models_dir"] = modelsDir,
            ["temp_dir"] = tempDir,
            ["device"] = gpuIndex is null ? "cpu" : "gpu",
            ["gpu_index"] = gpuIndex ?? 0,
            ["threads"] = threads,
        };
        if (transcriptionModel is not null) config["transcription_model"] = transcriptionModel;
        Native.Check(Native.EngineCreate(config.ToJsonString(), out var handle));
        Handle = handle;
    }

    public void Dispose()
    {
        if (Handle != 0) Native.EngineDestroy(Handle);
        Handle = 0;
    }
}

/// <summary>
/// sillok_job: one recording. The job keeps it as 16 kHz mono, the audio the
/// transcript's times refer to, which the app reads back for the listening
/// copy. <see cref="Cancel"/> may be called from any thread.
/// </summary>
public sealed unsafe class Job : IDisposable
{
    private readonly Lock _lock = new();
    private nint _handle;

    public Job(Engine engine, string language, bool diarize, IEnumerable<string> glossary)
    {
        var options = new JsonObject
        {
            ["language"] = language,
            ["diarize"] = diarize,
            ["glossary"] = new JsonArray(glossary.Select(t => (JsonNode?)t).ToArray()),
        };
        Native.Check(Native.JobCreate(engine.Handle, options.ToJsonString(), out _handle));
    }

    /// <summary>Appends decoded audio: frames of <paramref name="channels"/> interleaved samples in [-1, 1].</summary>
    public void PushAudio(ReadOnlySpan<float> samples, int sampleRate, int channels)
    {
        fixed (float* data = samples)
        {
            Native.Check(Native.JobPushAudio(_handle, data, (nuint)(samples.Length / channels), sampleRate, channels));
        }
    }

    /// <summary>Ends the recording; every sample of it becomes readable.</summary>
    public void EndAudio() => Native.Check(Native.JobEndAudio(_handle));

    /// <summary>Samples of the job's recording available so far.</summary>
    public long AudioLength
    {
        get
        {
            Native.Check(Native.JobAudioLength(_handle, out var length));
            return (long)length;
        }
    }

    /// <summary>Copies samples from <paramref name="start"/> into <paramref name="samples"/>; returns how many.</summary>
    public int ReadAudio(long start, Span<float> samples)
    {
        fixed (float* data = samples)
        {
            Native.Check(Native.JobReadAudio(_handle, (ulong)start, data, (nuint)samples.Length, out var read));
            return (int)read;
        }
    }

    /// <summary>
    /// Blocks until the transcript document is ready. <paramref name="progress"/>
    /// and <paramref name="segments"/> (segments as the transcription finishes
    /// them, previews of the transcript) run on this thread.
    /// </summary>
    public string Run(Action<Stage, double>? progress, Action<IReadOnlyList<Transcript.Segment>>? segments = null)
    {
        var state = new RunState(progress, segments);
        var handle = GCHandle.Alloc(state);
        try
        {
            if (segments is not null) Native.Check(Native.JobOnSegments(_handle, &ReportSegments, GCHandle.ToIntPtr(handle)));
            var status = Native.JobRun(_handle, &Report, GCHandle.ToIntPtr(handle), out var json);
            state.Failure?.Throw();
            Native.Check(status);
            return Native.Take(json);
        }
        finally
        {
            handle.Free();
        }
    }

    public void Cancel()
    {
        lock (_lock)
        {
            if (_handle != 0) Native.JobCancel(_handle);
        }
    }

    /// <summary>Must not be called while <see cref="Run"/> executes.</summary>
    public void Dispose()
    {
        lock (_lock)
        {
            if (_handle != 0) Native.JobDestroy(_handle);
            _handle = 0;
        }
    }

    private sealed class RunState(Action<Stage, double>? progress, Action<IReadOnlyList<Transcript.Segment>>? segments)
    {
        public Action<Stage, double>? Progress { get; } = progress;
        public Action<IReadOnlyList<Transcript.Segment>>? Segments { get; } = segments;
        public System.Runtime.ExceptionServices.ExceptionDispatchInfo? Failure { get; set; }
    }

    // Exceptions must not unwind into the library; the first one is
    // rethrown once the run returns, and later reports are dropped.
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Report(nint userData, int stage, double fraction)
    {
        var state = (RunState)GCHandle.FromIntPtr(userData).Target!;
        if (state.Failure is not null) return;
        try
        {
            state.Progress?.Invoke((Stage)stage, fraction);
        }
        catch (Exception e)
        {
            state.Failure = System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReportSegments(nint userData, nint json)
    {
        var state = (RunState)GCHandle.FromIntPtr(userData).Target!;
        if (state.Failure is not null) return;
        try
        {
            state.Segments?.Invoke(Transcript.ParseSegments(JsonNode.Parse(Marshal.PtrToStringUTF8(json) ?? "[]")!.AsArray()));
        }
        catch (Exception e)
        {
            state.Failure = System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e);
        }
    }
}
