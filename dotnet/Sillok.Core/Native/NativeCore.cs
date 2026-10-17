namespace Sillok.Core.Native;

public sealed class NativeCore(NativeEngine engine, string vadModel, string? diarizationModel = null) : ITranscriptionCore
{
    private const int VadWindowFrames = 1875;

    public Task<IReadOnlyList<AudioChunk>> PlanAsync(IAudioSource source, CancellationToken cancellation) =>
        NativeCall.Run<IReadOnlyList<AudioChunk>>(() =>
        {
            var vad = NativeMethods.sillok_vad_new(vadModel, out var error);
            if (vad == IntPtr.Zero) throw new NativeException(NativeCall.TakeString(error) ?? "failed to load the VAD model");
            try
            {
                var map = NativeMethods.sillok_speech_map_new(vad);
                try
                {
                    var window = NativeMethods.sillok_vad_frame_size() * VadWindowFrames / (double)PcmFormat.SampleRate;
                    for (double start = 0; start < source.Duration; start += window)
                    {
                        cancellation.ThrowIfCancellationRequested();
                        var samples = source.Read(start, start + window);
                        if (samples.Length == 0) break;
                        unsafe
                        {
                            fixed (float* pointer = samples)
                            {
                                NativeCall.Check(NativeMethods.sillok_speech_map_append(map, pointer, (nuint)samples.Length));
                            }
                        }
                    }
                    return NativeCall.Decode<List<AudioChunk>>(NativeMethods.sillok_speech_map_plan(map, source.Duration, null));
                }
                finally
                {
                    NativeMethods.sillok_speech_map_free(map);
                }
            }
            finally
            {
                NativeMethods.sillok_vad_free(vad);
            }
        });

    public async Task<TranscriptionState> StepAsync(
        TranscriptionState state, IReadOnlyList<AudioChunk> chunks, IAudioSource source, TranscriptionOptions options, CancellationToken cancellation)
    {
        var stateJson = SillokJson.Serialize(new StepState(state.NextChunk, state.PreviousLanguage, state.PreviousText));
        var chunksJson = SillokJson.Serialize(chunks);
        var optionsJson = SillokJson.Serialize(options);
        var result = await NativeCall.WithCancellation(cancellation, cancel =>
        {
            using var context = new AudioContext(source);
            unsafe
            {
                return NativeCall.Decode<StepResult>(NativeMethods.sillok_transcribe_step(
                    engine.Pointer, stateJson, chunksJson, optionsJson, &AudioContext.Read, context.Pointer, cancel));
            }
        });
        return state with
        {
            NextChunk = result.State.NextChunk,
            PreviousLanguage = result.State.PreviousLanguage,
            PreviousText = result.State.PreviousText,
            Segments = [.. state.Segments, .. result.Segments],
        };
    }

    public async Task<IReadOnlyList<Segment>> DiarizeAsync(IReadOnlyList<Segment> segments, IAudioSource source, CancellationToken cancellation)
    {
        if (diarizationModel is null || segments.Count == 0) return segments;
        await engine.UnloadAsync();
        var segmentsJson = SillokJson.Serialize(segments);
        return await NativeCall.WithCancellation<IReadOnlyList<Segment>>(cancellation, cancel =>
        {
            var diarizer = NativeMethods.sillok_diarizer_new(diarizationModel, 1, out var error);
            if (diarizer == IntPtr.Zero) throw new NativeException(NativeCall.TakeString(error) ?? "failed to load the diarization model");
            try
            {
                using var context = new AudioContext(source);
                unsafe
                {
                    return NativeCall.Decode<List<Segment>>(NativeMethods.sillok_diarize(
                        diarizer, segmentsJson, source.Duration, &AudioContext.Read, context.Pointer, cancel));
                }
            }
            finally
            {
                NativeMethods.sillok_diarizer_free(diarizer);
            }
        });
    }

    private sealed record StepState(int NextChunk, string? PreviousLanguage, string? PreviousText);

    private sealed record StepResult(StepState State, List<Segment> Segments);
}

public enum ExportFormat
{
    Text,
    SubRip,
}

public static class NativeExport
{
    public static string Render(IReadOnlyList<Segment> segments, ExportFormat format, string? speakerFormat = null) =>
        NativeCall.Decode<string>(NativeMethods.sillok_export(SillokJson.Serialize(segments), format == ExportFormat.SubRip ? "srt" : "txt", speakerFormat));
}
