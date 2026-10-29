namespace Sillok;

/// <summary>The steps of a transcription as the apps show them: reading the file, then the library's stages.</summary>
public enum RunStep { Read, Analyze, Transcribe, Diarize }

/// <summary>Transcription settings. <see cref="GpuIndex"/> null means the CPU.</summary>
public sealed record JobSettings(string Language = "auto", bool Diarize = true, string Glossary = "", int? GpuIndex = null)
{
    /// <summary>Glossary terms, one per line or separated by commas.</summary>
    public IReadOnlyList<string> Terms =>
        Glossary.Split(['\n', '\r', ','], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    public static RunStep ToStep(Stage stage) => stage switch
    {
        Stage.Analyze => RunStep.Analyze,
        Stage.Transcribe => RunStep.Transcribe,
        _ => RunStep.Diarize,
    };
}
