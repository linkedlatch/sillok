using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Pages;

/// <summary>Downloads the models before first use: progress, pause and resume.</summary>
public sealed partial class ModelsPage : Page
{
    private static AppState State => App.State;

    public ModelsPage()
    {
        InitializeComponent();
        Heading.Text = Strings.Get("ModelsTitle");
        StopButton.Content = Strings.Get("ModelsStop");
        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            Update();
        };
        Unloaded += (_, _) => State.PropertyChanged -= OnStateChanged;
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e) => Update();

    private void Update()
    {
        var phase = State.ModelPhase;
        Body.Text = phase is ModelPhase.Checking ? Strings.Get("ModelsChecking")
            : phase is ModelPhase.Verifying ? Strings.Get("ModelsVerifying")
            : Strings.Format("ModelsBody", Strings.Bytes(State.ModelTotal));
        ErrorBar.Message = State.ModelError ?? "";
        ErrorBar.IsOpen = State.ModelError is not null;
        Bar.IsIndeterminate = phase is ModelPhase.Checking or ModelPhase.Verifying;
        Bar.Value = State.ModelTotal > 0 ? (double)State.ModelDone / State.ModelTotal : 0;
        Bar.Visibility = phase == ModelPhase.Missing && State.ModelDone == 0 ? Visibility.Collapsed : Visibility.Visible;
        Status.Text = phase == ModelPhase.Downloading
            ? Strings.Format("ModelsProgress", Strings.Bytes(State.ModelDone), Strings.Bytes(State.ModelTotal), Strings.Bytes(State.ModelSpeed))
            : "";
        DownloadButton.Content = Strings.Get(State.ModelDone > 0 ? "ModelsResume" : "ModelsDownload");
        DownloadButton.Visibility = phase == ModelPhase.Missing ? Visibility.Visible : Visibility.Collapsed;
        StopButton.Visibility = phase == ModelPhase.Downloading ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnDownload(object sender, RoutedEventArgs e) => _ = State.StartDownloadAsync();

    private void OnStop(object sender, RoutedEventArgs e) => State.StopDownload();
}
