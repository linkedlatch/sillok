using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Pages;

/// <summary>Settings for the file about to be transcribed, remembered between runs.</summary>
public sealed partial class SetupPage : Page
{
    private static AppState State => App.State;
    private static readonly string[] Languages = ["auto", "ko", "en"];

    public SetupPage()
    {
        InitializeComponent();
        var settings = State.Settings;
        Heading.Text = Strings.Get("SetupTitle");
        FileName.Text = State.PendingFile?.Name ?? "";

        Language.Header = Strings.Get("SetupLanguage");
        foreach (var code in Languages) Language.Items.Add(Strings.Get("Language_" + code));
        Language.SelectedIndex = Math.Max(0, Array.IndexOf(Languages, settings.Language));

        Diarize.Header = Strings.Get("SetupDiarize");
        Diarize.IsOn = settings.Diarize;

        Glossary.Header = Strings.Get("SetupGlossary");
        Glossary.PlaceholderText = Strings.Get("SetupGlossaryHint");
        Glossary.Text = settings.Glossary;

        // The CPU first, then each GPU in the library's order.
        Device.Header = Strings.Get("SetupDevice");
        Device.Items.Add(Strings.Get("DeviceCpu"));
        foreach (var gpu in State.Gpus) Device.Items.Add(gpu.Description);
        Device.SelectedIndex = settings.GpuIndex is { } index ? index + 1 : 0;
        Device.Visibility = State.Gpus.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

        StartButton.Content = Strings.Get("SetupStart");
        CancelButton.Content = Strings.Get("Cancel");
    }

    private void OnStart(object sender, RoutedEventArgs e) => State.Start(new JobSettings(
        Language: Languages[Math.Max(0, Language.SelectedIndex)],
        Diarize: Diarize.IsOn,
        Glossary: Glossary.Text,
        GpuIndex: Device.SelectedIndex > 0 ? Device.SelectedIndex - 1 : null));

    private void OnCancel(object sender, RoutedEventArgs e) => State.GoHome();
}
