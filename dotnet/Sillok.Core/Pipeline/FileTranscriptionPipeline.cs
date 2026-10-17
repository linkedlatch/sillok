using System.Runtime.CompilerServices;

namespace Sillok.Core;

public interface ITranscriptionCore
{
    Task<IReadOnlyList<AudioChunk>> PlanAsync(IAudioSource source, CancellationToken cancellation);

    Task<TranscriptionState> StepAsync(
        TranscriptionState state, IReadOnlyList<AudioChunk> chunks, IAudioSource source, TranscriptionOptions options, CancellationToken cancellation);

    Task<IReadOnlyList<Segment>> DiarizeAsync(IReadOnlyList<Segment> segments, IAudioSource source, CancellationToken cancellation);
}

public enum TranscribePhase
{
    Analyzing,
    Transcribing,
    Diarizing,
}

public sealed record TranscribeProgress(
    TranscribePhase Phase,
    double FractionCompleted,
    double LastProcessedTimestamp,
    IReadOnlyList<Segment> NewSegments,
    bool ReplacesSegments = false);

public sealed class FileTranscriptionPipeline(ITranscriptionCore core, TranscriptionOptions options, string modelId, string checkpointPath)
{
    public async IAsyncEnumerable<TranscribeProgress> RunAsync(IAudioSource source, [EnumeratorCancellation] CancellationToken cancellation = default)
    {
        var checkpoint = LoadMatchingCheckpoint();
        if (checkpoint is not null)
        {
            yield return new(TranscribePhase.Transcribing, Fraction(checkpoint.ResumeFrom, source.Duration), checkpoint.ResumeFrom, checkpoint.State.Segments);
        }
        else
        {
            yield return new(TranscribePhase.Analyzing, 0, 0, []);
            var chunks = await core.PlanAsync(source, cancellation);
            checkpoint = new Checkpoint { ModelId = modelId, Options = options, Chunks = chunks };
            VersionedStore.Save(checkpoint, checkpointPath);
        }

        while (!checkpoint.State.IsComplete(checkpoint.Chunks.Count))
        {
            cancellation.ThrowIfCancellationRequested();
            var before = checkpoint.State.Segments.Count;
            checkpoint = checkpoint with { State = await core.StepAsync(checkpoint.State, checkpoint.Chunks, source, options, cancellation) };
            VersionedStore.Save(checkpoint, checkpointPath);
            yield return new(
                TranscribePhase.Transcribing,
                Fraction(checkpoint.ResumeFrom, source.Duration),
                checkpoint.ResumeFrom,
                checkpoint.State.Segments.Skip(before).ToList());
        }

        if (options.SpeakerDiarization && !checkpoint.State.Diarized)
        {
            cancellation.ThrowIfCancellationRequested();
            yield return new(TranscribePhase.Diarizing, 1, source.Duration, []);
            var segments = await core.DiarizeAsync(checkpoint.State.Segments, source, cancellation);
            checkpoint = checkpoint with { State = checkpoint.State with { Segments = segments, Diarized = true } };
            VersionedStore.Save(checkpoint, checkpointPath);
            yield return new(TranscribePhase.Diarizing, 1, source.Duration, segments, ReplacesSegments: true);
        }

        yield return new(options.SpeakerDiarization ? TranscribePhase.Diarizing : TranscribePhase.Transcribing, 1, source.Duration, []);
    }

    private Checkpoint? LoadMatchingCheckpoint()
    {
        if (!File.Exists(checkpointPath)) return null;
        try
        {
            var saved = VersionedStore.Load<Checkpoint>(checkpointPath);
            return saved.ModelId == modelId && saved.Options.SameAs(options) ? saved : null;
        }
        catch (Exception)
        {
            return null;
        }
    }

    private static double Fraction(double time, double duration) => duration > 0 ? Math.Min(time / duration, 1) : 1;
}
