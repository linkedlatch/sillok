using System.ComponentModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;
using Sillok.App.Pages;

namespace Sillok.App;

/// <summary>
/// Shows the page for the state: the first start until a model is here, the
/// settings, or the library. Messages appear in an info bar; the title shows
/// the running job's progress, and a notification tells a user who is in
/// another window that a file is done.
/// </summary>
public sealed partial class MainWindow : Window
{
    private static AppState State => App.State;
    private bool _closeConfirmed;
    private bool _active = true;

    public MainWindow()
    {
        InitializeComponent();
        Title = Strings.Get("AppName");
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1200, 800));
        AppWindow.Closing += OnClosing;
        Activated += (_, args) => _active = args.WindowActivationState != WindowActivationState.Deactivated;
        State.PropertyChanged += OnStateChanged;
        Show();
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(AppState.ModelsChecked) or nameof(AppState.Models) or nameof(AppState.ShowSettings) or nameof(AppState.Library):
                Show();
                if (e.PropertyName == nameof(AppState.Library)) ShowProgress();
                break;
            case nameof(AppState.Message) when State.Message is { } message:
                State.Message = null;
                Notice.Message = message;
                Notice.Severity = InfoBarSeverity.Informational;
                Notice.IsOpen = true;
                break;
            case nameof(AppState.Finished) when State.Finished is { } finished && !_active:
                Notify(finished.Id);
                break;
        }
    }

    private void Show()
    {
        if (!State.ModelsChecked) return;
        var page = State.ReadyTranscriptionModels.Count == 0 && State.Library.Count == 0 ? typeof(WelcomePage)
            : State.ShowSettings ? typeof(SettingsPage)
            : typeof(LibraryPage);
        if (Root.Content?.GetType() != page) Root.Navigate(page);
    }

    /// <summary>"Sillok — Transcribing 34%" while a file runs.</summary>
    private void ShowProgress()
    {
        Title = State.Run is { } run
            ? $"{Strings.Get("AppName")} — {Strings.Format("NotificationProgress", Strings.Step(run.Step), (int)(run.Overall * 100))}"
            : Strings.Get("AppName");
    }

    private static void Notify(string id)
    {
        if (State.Entry(id) is not { } entry) return;
        try
        {
            AppNotificationManager.Default.Show(new AppNotificationBuilder()
                .AddText(entry.Title)
                .AddText(Strings.Get("NotificationDone"))
                .BuildNotification());
        }
        catch (Exception e) when (e is InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            // Notifications are a convenience; the list shows the result anyway.
        }
    }

    // Closing the window ends the queue's work; ask first.
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
