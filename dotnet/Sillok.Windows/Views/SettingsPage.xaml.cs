using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sillok.Core;

namespace Sillok.Windows.Views;

public sealed record LanguageChoice(string Name, LanguageMode Mode);

public sealed record Notice(string Title, string Text);

public sealed partial class SettingsPage : Page
{
    private static readonly LanguageChoice[] Languages =
    [
        new("자동 감지", LanguageMode.Auto),
        new("한국어 + 영어 혼합", LanguageMode.KoreanEnglish),
        new("한국어", LanguageMode.Fixed("ko")),
        new("영어", LanguageMode.Fixed("en")),
        new("일본어", LanguageMode.Fixed("ja")),
        new("중국어", LanguageMode.Fixed("zh")),
        new("스페인어", LanguageMode.Fixed("es")),
        new("프랑스어", LanguageMode.Fixed("fr")),
        new("독일어", LanguageMode.Fixed("de")),
    ];

    private static readonly (string Title, string File)[] NoticeFiles =
    [
        ("whisper.cpp", "whisper.cpp.txt"),
        ("OpenAI Whisper (모델 가중치)", "openai-whisper.txt"),
        ("Silero VAD", "silero-vad.txt"),
        ("NeMo-Speech.cpp", "nemo-speech-cpp.txt"),
        ("NVIDIA Nemotron 3 Diarization (모델 가중치)", "nemotron-3-diarization.txt"),
    ];

    public SettingsPage()
    {
        InitializeComponent();
        LanguagePicker.ItemsSource = Languages;
        LanguagePicker.SelectedItem = Languages.FirstOrDefault(choice => choice.Mode == Model.LanguageMode) ?? Languages[0];
        Model.RefreshModelStatus();
    }

    public AppModel Model => AppModel.Shared;

    public IReadOnlyList<Notice> Notices { get; } = NoticeFiles
        .Select(notice => new Notice(notice.Title, ReadNotice(notice.File)))
        .ToList();

    private void OnLanguageChanged(object sender, SelectionChangedEventArgs e)
    {
        if (LanguagePicker.SelectedItem is LanguageChoice choice) Model.LanguageMode = choice.Mode;
    }

    private void OnRemoveModel(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string id }) Model.RemoveModel(id);
    }

    private static string ReadNotice(string file)
    {
        var path = Path.Combine(AppContext.BaseDirectory, "Licenses", file);
        return File.Exists(path) ? File.ReadAllText(path) : "";
    }
}
