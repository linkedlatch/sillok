using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Views;

/// <summary>Confirmations and small forms as content dialogs.</summary>
internal static class Dialogs
{
    private static AppState State => App.State;

    public static async Task RenameAsync(XamlRoot root, JobEntry entry)
    {
        var box = new TextBox { Text = entry.Title, Header = Strings.Get("Title") };
        box.Loaded += (_, _) => box.SelectAll();
        var dialog = new ContentDialog
        {
            XamlRoot = root,
            Title = Strings.Get("Rename"),
            Content = box,
            PrimaryButtonText = Strings.Get("Save"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) State.Rename(entry, box.Text);
    }

    public static async Task DeleteAsync(XamlRoot root, JobEntry entry)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = root,
            Content = Strings.Format("DeleteConfirm", entry.Title),
            PrimaryButtonText = Strings.Get("Delete"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Close,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) State.Delete(entry.Id);
    }

    public static async Task MessageAsync(XamlRoot root, string title, string text)
    {
        await new ContentDialog
        {
            XamlRoot = root,
            Title = title,
            Content = new ScrollViewer { Content = new TextBlock { Text = text, IsTextSelectionEnabled = true, TextWrapping = TextWrapping.Wrap } },
            CloseButtonText = Strings.Get("Ok"),
        }.ShowAsync();
    }
}

/// <summary>
/// Confirms files to transcribe: their names and a summary of the defaults,
/// with the options in an expander for when they should differ.
/// </summary>
internal static class AddDialog
{
    private static AppState State => App.State;

    public static async Task ShowAsync(XamlRoot root, IReadOnlyList<string> paths)
    {
        var fields = new SettingsFields(State.Defaults, State.ReadyTranscriptionModels, State.Gpus);
        var summary = new TextBlock { TextWrapping = TextWrapping.Wrap, Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"] };
        var missing = new InfoBar { Severity = InfoBarSeverity.Warning, IsClosable = false };
        var names = new StackPanel();
        foreach (var path in paths.Take(5)) names.Children.Add(new TextBlock { Text = Path.GetFileName(path), TextTrimming = TextTrimming.CharacterEllipsis });
        if (paths.Count > 5) names.Children.Add(new TextBlock { Text = "…" });
        var dialog = new ContentDialog
        {
            XamlRoot = root,
            Title = paths.Count == 1 ? Strings.Get("AddTitleOne") : Strings.Format("AddTitle", paths.Count),
            Content = new ScrollViewer
            {
                Content = new StackPanel
                {
                    Spacing = 12,
                    Children =
                    {
                        names,
                        summary,
                        missing,
                        new Expander { Header = Strings.Get("AddOptions"), Content = fields, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch },
                    },
                },
            },
            PrimaryButtonText = Strings.Get("AddStart"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Primary,
        };
        void Update(JobSettings settings)
        {
            var model = State.Catalog.FirstOrDefault(m => m.Id == settings.TranscriptionModel) ?? State.RecommendedTranscription;
            summary.Text = Strings.SettingsSummary(settings, model);
            var ready = State.ModelsReady(settings.TranscriptionModel);
            missing.Message = Strings.Format("AddModelMissing", Strings.ModelName(model));
            missing.IsOpen = !ready;
            dialog.IsPrimaryButtonEnabled = ready;
        }
        fields.Changed += (_, settings) => Update(settings);
        Update(fields.Settings);
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) State.Enqueue(paths, fields.Settings);
    }
}
