namespace Sillok.Core;

public sealed record WordTimestamp(string Word, double Start, double End, float Probability);

public sealed record Segment
{
    public string Id { get; init; } = Guid.NewGuid().ToString().ToUpperInvariant();
    public double Start { get; init; }
    public double End { get; init; }
    public string Text { get; init; } = "";
    public string Language { get; init; } = "";
    public string? Speaker { get; init; }
    public IReadOnlyList<WordTimestamp>? Words { get; init; }
    public float? AvgLogprob { get; init; }
    public float? NoSpeechProb { get; init; }
    public float? CompressionRatio { get; init; }
    public bool IsLowConfidence { get; init; }
}

public sealed record AudioChunk(double Start, double End, double? CutBefore = null, double? CutAfter = null);
