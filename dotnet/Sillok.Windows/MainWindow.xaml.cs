using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sillok.Windows.Views;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace Sillok.Windows;

public sealed partial class MainWindow : Window
{
    private static readonly string[] AudioExtensions = [".m4a", ".mp3", ".wav", ".aac", ".flac", ".wma", ".mp4", ".mov", ".ogg", ".opus"];

    public MainWindow()
    {
        InitializeComponent();
        AppWindow.SetIcon("Assets/Square44x44Logo.png");
        AppWindow.Changed += OnAppWindowChanged;
        Model.AlertRaised += ShowAlert;
    }

    public AppModel Model => AppModel.Shared;

    private void OnAppWindowChanged(AppWindow sender, AppWindowChangedEventArgs args)
    {
        if (!args.DidPresenterChange && !args.DidVisibilityChange) return;
        var minimized = sender.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized };
        if (minimized || !sender.IsVisible) Model.Playback.Pause();
    }

    private void OnSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.IsSettingsSelected)
        {
            ContentFrame.Navigate(typeof(SettingsPage));
        }
        else if (args.SelectedItem is ProjectItem item)
        {
            ContentFrame.Navigate(typeof(TranscriptPage), item);
        }
    }

    private async void OnAddFiles(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker();
        foreach (var extension in AudioExtensions) picker.FileTypeFilter.Add(extension);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
        var files = await picker.PickMultipleFilesAsync();
        if (files.Count > 0) Model.AddFiles(files.Select(file => file.Path));
    }

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (e.DataView.Contains(StandardDataFormats.StorageItems)) e.AcceptedOperation = DataPackageOperation.Copy;
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        var items = await e.DataView.GetStorageItemsAsync();
        var paths = items.OfType<StorageFile>().Select(file => file.Path)
            .Where(path => AudioExtensions.Contains(Path.GetExtension(path).ToLowerInvariant())).ToList();
        if (paths.Count < items.Count) ShowAlert("지원하지 않는 파일은 건너뛰었습니다");
        Model.AddFiles(paths);
    }

    private void OnModelAction(object sender, RoutedEventArgs e) => Model.ModelAction();

    private void OnCancelProject(object sender, RoutedEventArgs e) => Model.Cancel(ProjectId(sender));

    private void OnRetryProject(object sender, RoutedEventArgs e) => Model.Retry(ProjectId(sender));

    private void OnDeleteProject(object sender, RoutedEventArgs e)
    {
        var id = ProjectId(sender);
        if (ContentFrame.Content is TranscriptPage { Item: { } shown } && shown.Id == id) ContentFrame.Content = null;
        Model.Delete(id);
    }

    private static Guid ProjectId(object sender) => (Guid)((FrameworkElement)sender).Tag;

    private async void ShowAlert(string message)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = "Sillok",
            Content = message,
            CloseButtonText = "확인",
        };
        await dialog.ShowAsync();
    }
}
