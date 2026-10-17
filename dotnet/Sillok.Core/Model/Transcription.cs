namespace Sillok.Core;

public sealed record TranscriptionOptions
{
    public LanguageMode LanguageMode { get; init; } = LanguageMode.Auto;
    public IReadOnlyList<string> Vocabulary { get; init; } = [];
    public int BeamSize { get; init; } = 5;
    public int MaxPromptTokens { get; init; } = 150;
    public bool SpeakerDiarization { get; init; }

    public bool SameAs(TranscriptionOptions other) => SillokJson.Serialize(this) == SillokJson.Serialize(other);
}

public sealed record TranscriptionState
{
    public int NextChunk { get; init; }
    public string? PreviousLanguage { get; init; }
    public string? PreviousText { get; init; }
    public IReadOnlyList<Segment> Segments { get; init; } = [];
    public bool Diarized { get; init; }

    public bool IsComplete(int chunkCount) => NextChunk >= chunkCount;
}

public sealed record ModelFiles(string Ggml, string? CoreMLEncoder = null);
