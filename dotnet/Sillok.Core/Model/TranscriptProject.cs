namespace Sillok.Core;

public enum TranscriptStatus
{
    Pending,
    Transcribing,
    Completed,
    Cancelled,
    Failed,
}

public sealed record TranscriptProject : IVersionedDocument<TranscriptProject>
{
    public static int CurrentSchemaVersion => 1;

    public int SchemaVersion { get; init; } = CurrentSchemaVersion;
    public Guid Id { get; init; } = Guid.NewGuid();
    public DateTimeOffset CreatedAt { get; init; } = DateTimeOffset.Now;
    public DateTimeOffset UpdatedAt { get; init; } = DateTimeOffset.Now;
    public required string SourceName { get; init; }
    public required string SourcePath { get; init; }
    public double? Duration { get; init; }
    public string? ModelId { get; init; }
    public TranscriptionOptions Options { get; init; } = new();
    public TranscriptStatus Status { get; init; } = TranscriptStatus.Pending;
    public string? FailureMessage { get; init; }
    public IReadOnlyList<Segment> Segments { get; init; } = [];
}
