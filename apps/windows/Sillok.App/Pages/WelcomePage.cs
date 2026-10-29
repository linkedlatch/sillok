using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Pages;

/// <summary>The first start: what Sillok does on this computer, and the recommended models to download.</summary>
public sealed partial class WelcomePage : Page
{
    private static AppState State => App.State;
    private readonly StackPanel _download = new() { Spacing = 8 };

    public WelcomePage()
    {
        var size = State.Catalog.Where(m => m.Recommended).Sum(m => m.Size);
        Content = new ScrollViewer
        {
            Content = new StackPanel
            {
                MaxWidth = 640,
                Margin = new Thickness(32),
                Spacing = 16,
                Children =
                {
                    new TextBlock { Text = Strings.Get("WelcomeTitle"), Style = Theme.Style("TitleTextBlockStyle") },
                    new TextBlock { Text = Strings.Format("WelcomeBody", Strings.Bytes(size)), TextWrapping = TextWrapping.Wrap },
                    _download,
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
        if (e.PropertyName == nameof(AppState.Models)) Update();
    }

    private void Update()
    {
        _download.Children.Clear();
        _download.Children.Add(ModelProgress.For(State, State.RecommendedTranscription));
    }
}

/// <summary>Resources of the app's theme, for controls built in code.</summary>
internal static class Theme
{
    public static Style Style(string key) => (Style)Application.Current.Resources[key];
}

/// <summary>Download progress of a model, or the button to download it.</summary>
internal static class ModelProgress
{
    public static FrameworkElement For(AppState state, CatalogModel model)
    {
        switch (state.Models.GetValueOrDefault(model.Id))
        {
            case ModelStatus.Downloading d:
                var stop = new Button { Content = Strings.Get("ModelsStop") };
                stop.Click += (_, _) => state.StopDownload();
                return new StackPanel
                {
                    Spacing = 8,
                    Children =
                    {
                        new ProgressBar { Maximum = 1, Value = (double)d.Done / Math.Max(d.Total, 1) },
                        new TextBlock
                        {
                            Text = Strings.Format("ModelsProgress", Strings.Bytes(d.Done), Strings.Bytes(d.Total), Strings.Bytes(d.BytesPerSecond)),
                            Style = Theme.Style("CaptionTextBlockStyle"),
                        },
                        stop,
                    },
                };
            case ModelStatus.Verifying:
                return new StackPanel
                {
                    Spacing = 8,
                    Children =
                    {
                        new ProgressBar { IsIndeterminate = true },
                        new TextBlock { Text = Strings.Get("ModelsVerifying"), Style = Theme.Style("CaptionTextBlockStyle") },
                    },
                };
            default:
                var started = state.Models.GetValueOrDefault(model.Id) is ModelStatus.Missing { Downloaded: > 0 };
                var download = new Button
                {
                    Content = Strings.Get(started ? "ModelsResume" : "ModelsDownload"),
                    Style = Theme.Style("AccentButtonStyle"),
                };
                download.Click += (_, _) => _ = state.DownloadModelAsync(model.Id);
                return download;
        }
    }
}
