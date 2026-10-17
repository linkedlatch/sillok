using Sillok.Core;

namespace Sillok.Windows;

public sealed record AppSettings
{
    public string WhisperId { get; init; } = ModelSelection.DefaultWhisperId;
    public LanguageMode LanguageMode { get; init; } = LanguageMode.Auto;
    public string VocabularyText { get; init; } = "";
    public bool SpeakerDiarization { get; init; } = true;
    public bool UseGpu { get; init; } = true;

    public IReadOnlyList<string> Vocabulary =>
        VocabularyText.Split('\n', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);

    public TranscriptionOptions TranscriptionOptions => new()
    {
        LanguageMode = LanguageMode,
        Vocabulary = Vocabulary,
        SpeakerDiarization = SpeakerDiarization,
    };

    public static AppSettings Load(string path)
    {
        try
        {
            return File.Exists(path) ? SillokJson.Deserialize<AppSettings>(File.ReadAllText(path)) : new AppSettings();
        }
        catch (Exception)
        {
            return new AppSettings();
        }
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, SillokJson.Serialize(this));
    }
}
