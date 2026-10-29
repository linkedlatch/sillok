using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Sillok.App.Views;

namespace Sillok.App.Pages;

/// <summary>
/// Settings in the Windows 11 way: a back button, a title and cards in
/// groups — defaults for new files, transcription models, storage, about.
/// </summary>
public sealed partial class SettingsPage : Page
{
    private static AppState State => App.State;
    private readonly StackPanel _models = new() { Spacing = 4 };
    private readonly TextBlock _storage = new() { TextWrapping = TextWrapping.Wrap };

    public SettingsPage()
    {
        var back = new Button { Content = new SymbolIcon(Symbol.Back), Padding = new Thickness(8) };
        ToolTipService.SetToolTip(back, Strings.Get("Back"));
        back.Click += (_, _) => State.ShowSettings = false;
        back.KeyboardAccelerators.Add(new Microsoft.UI.Xaml.Input.KeyboardAccelerator { Key = Windows.System.VirtualKey.Escape });

        var defaults = new SettingsFields(State.Defaults, [], State.Gpus);
        defaults.Changed += (_, settings) => State.SaveDefaults(settings);

        var verify = new Button { Content = Strings.Get("MenuVerifyModels") };
        verify.Click += (_, _) => _ = State.VerifyModelsAsync();
        var about = new TextBlock { Text = AppState.Version, IsTextSelectionEnabled = true, Style = Theme.Style("CaptionTextBlockStyle") };
        var folder = new HyperlinkButton { Content = Strings.Get("OpenDataFolder") };
        folder.Click += (_, _) => System.Diagnostics.Process.Start("explorer.exe", $"\"{AppState.DataDir}\"");

        Content = new ScrollViewer
        {
            Content = new StackPanel
            {
                MaxWidth = 1000,
                Margin = new Thickness(36, 24, 36, 36),
                Spacing = 8,
                Children =
                {
                    new StackPanel
                    {
                        Orientation = Orientation.Horizontal,
                        Spacing = 12,
                        Children = { back, new TextBlock { Text = Strings.Get("Settings"), Style = Theme.Style("TitleTextBlockStyle") } },
                    },
                    Header(Strings.Get("SettingsDefaults")),
                    Card(defaults),
                    Header(Strings.Get("SettingsModels")),
                    new TextBlock { Text = Strings.Get("SettingsModelsHint"), TextWrapping = TextWrapping.Wrap, Style = Theme.Style("CaptionTextBlockStyle") },
                    _models,
                    Header(Strings.Get("SettingsStorage")),
                    Card(new StackPanel { Spacing = 8, Children = { _storage, folder } }),
                    Header(Strings.Get("MenuAbout")),
                    Card(new StackPanel { Spacing = 8, Children = { about, verify } }),
                },
            },
        };
        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            Update();
        };
        Unloaded += (_, _) => State.PropertyChanged -= OnStateChanged;
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.Models) or nameof(AppState.Defaults)) Update();
    }

    private void Update()
    {
        var inUse = State.Defaults.TranscriptionModel ?? State.RecommendedTranscription.Id;
        _models.Children.Clear();
        foreach (var model in State.TranscriptionModels) _models.Children.Add(ModelCard(model, model.Id == inUse));
        var (models, history) = State.StorageUse();
        _storage.Text = Strings.Format("SettingsStorageModels", Strings.Bytes(models)) + "\n" +
                        Strings.Format("SettingsStorageHistory", Strings.Bytes(history));
    }

    /// <summary>A model: what it is, and whether it is here (choose, delete), downloading or to download.</summary>
    private static Border ModelCard(CatalogModel model, bool inUse)
    {
        var ready = State.Models.GetValueOrDefault(model.Id) is ModelStatus.Ready;
        var title = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        title.Children.Add(new TextBlock { Text = Strings.ModelName(model), Style = Theme.Style("BodyStrongTextBlockStyle") });
        if (model.Recommended)
        {
            title.Children.Add(new TextBlock
            {
                Text = Strings.Get("ModelRecommended"),
                Foreground = (Brush)Application.Current.Resources["AccentTextFillColorPrimaryBrush"],
            });
        }
        var text = new StackPanel { Spacing = 2, VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(title);
        text.Children.Add(new TextBlock { Text = Strings.ModelSummary(model), Style = Theme.Style("CaptionTextBlockStyle") });
        if (inUse && ready) text.Children.Add(new TextBlock { Text = Strings.Get("ModelInUse"), Style = Theme.Style("CaptionTextBlockStyle") });

        var use = new RadioButton { IsChecked = inUse, IsEnabled = ready, GroupName = "model", MinWidth = 0, VerticalAlignment = VerticalAlignment.Center };
        use.Checked += (_, _) => State.SaveDefaults(State.Defaults with { TranscriptionModel = model.Recommended ? null : model.Id });

        FrameworkElement action;
        if (ready)
        {
            var delete = new Button { Content = Strings.Get("ModelDelete"), IsEnabled = !inUse };
            delete.Click += (_, _) => _ = State.DeleteModelAsync(model.Id);
            action = delete;
        }
        else
        {
            action = ModelProgress.For(State, model);
            action.MinWidth = 200;
        }

        var row = new Grid { ColumnSpacing = 12 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.Children.Add(use);
        Grid.SetColumn(text, 1);
        row.Children.Add(text);
        Grid.SetColumn(action, 2);
        row.Children.Add(action);
        return Card(row);
    }

    private static TextBlock Header(string text) =>
        new() { Text = text, Style = Theme.Style("BodyStrongTextBlockStyle"), Margin = new Thickness(0, 20, 0, 4) };

    /// <summary>A settings card: the Windows 11 Settings app's raised panel.</summary>
    private static Border Card(UIElement content) => new()
    {
        Child = content,
        Padding = new Thickness(16),
        CornerRadius = new CornerRadius(4),
        BorderThickness = new Thickness(1),
        Background = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"],
        BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
    };
}
