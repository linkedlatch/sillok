using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Sillok.Core.Native;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace Sillok.Windows.Views;

public sealed partial class TranscriptPage : Page
{
    public TranscriptPage()
    {
        InitializeComponent();
    }

    public ProjectItem? Item { get; private set; }

    public AppModel Model => AppModel.Shared;

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        Item = e.Parameter as ProjectItem;
        if (Item is not null) Item.PlayingRowChanged += OnPlayingRowChanged;
        Bindings.Update();
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        if (Item is not null) Item.PlayingRowChanged -= OnPlayingRowChanged;
    }

    private void OnPlayingRowChanged(SegmentRow row)
    {
        if (Model.Playback.IsPlaying) SegmentList.ScrollIntoView(row);
    }

    private void OnPlay(object sender, RoutedEventArgs e)
    {
        if (Item is not null && sender is FrameworkElement { Tag: double start }) Model.Play(Item, start);
    }

    private void OnPause(object sender, RoutedEventArgs e) => Model.Playback.Pause();

    private async void OnExportText(object sender, RoutedEventArgs e) => await ExportAsync(ExportFormat.Text, ".txt", "텍스트");

    private async void OnExportSubRip(object sender, RoutedEventArgs e) => await ExportAsync(ExportFormat.SubRip, ".srt", "자막");

    private async Task ExportAsync(ExportFormat format, string extension, string description)
    {
        if (Item is null || Item.Project.Segments.Count == 0) return;
        var picker = new FileSavePicker { SuggestedFileName = Path.GetFileNameWithoutExtension(Item.Name) };
        picker.FileTypeChoices.Add(description, [extension]);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(App.MainWindow));
        var file = await picker.PickSaveFileAsync();
        if (file is null) return;
        await FileIO.WriteTextAsync(file, NativeExport.Render(Item.Project.Segments, format, UserMessage.SpeakerFormat));
    }
}
