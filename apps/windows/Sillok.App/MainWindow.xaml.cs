using System.ComponentModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sillok.App.Pages;

namespace Sillok.App;

/// <summary>Shows the page for the state: the models first, then the screen the state names.</summary>
public sealed partial class MainWindow : Window
{
    private static AppState State => App.State;
    private bool _closeConfirmed;

    public MainWindow()
    {
        InitializeComponent();
        Title = Strings.Get("AppName");
        AppWindow.Resize(new Windows.Graphics.SizeInt32(960, 760));
        State.PropertyChanged += OnStateChanged;
        AppWindow.Closing += OnClosing;
        Show();
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.ModelPhase) or nameof(AppState.Screen)) Show();
        if (e.PropertyName == nameof(AppState.Error) && State.Error is { } message)
        {
            State.Error = null;
            _ = ShowMessageAsync(message);
        }
    }

    private void Show()
    {
        var page = State.ModelPhase != ModelPhase.Ready
            ? typeof(ModelsPage)
            : State.Screen switch
            {
                Screen.Setup => typeof(SetupPage),
                Screen.Running => typeof(RunningPage),
                Screen.Result => typeof(ResultPage),
                _ => typeof(HomePage),
            };
        if (Root.Content?.GetType() != page) Root.Navigate(page);
    }

    private async Task ShowMessageAsync(string message)
    {
        if (Root.XamlRoot is null) return;
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true },
            CloseButtonText = Strings.Get("Ok"),
        };
        await dialog.ShowAsync();
    }

    // Closing the window ends a running transcription; ask first.
    private async void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (State.Run is null || _closeConfirmed) return;
        args.Cancel = true;
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            Title = Strings.Get("CloseRunningTitle"),
            Content = Strings.Get("CloseRunningBody"),
            PrimaryButtonText = Strings.Get("CloseRunningConfirm"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Close,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary)
        {
            _closeConfirmed = true;
            State.Cancel();
            Close();
        }
    }
}
