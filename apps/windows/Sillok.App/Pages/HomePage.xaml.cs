using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Pages;

/// <summary>A row of the history list.</summary>
public sealed record HistoryRow(string Id, string Title, string Detail, string RenameText, string DeleteText);

/// <summary>The history, the running job and the way to open a file.</summary>
public sealed partial class HomePage : Page
{
    private static AppState State => App.State;

    public HomePage()
    {
        InitializeComponent();
        OpenButton.Label = Strings.Get("HomeOpen");
        VerifyButton.Label = Strings.Get("MenuVerifyModels");
        AboutButton.Label = Strings.Get("MenuAbout");
        Empty.Text = Strings.Get("HomeEmpty");
        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            State.History.CollectionChanged += OnHistoryChanged;
            UpdateList();
            UpdateRun();
        };
        Unloaded += (_, _) =>
        {
            State.PropertyChanged -= OnStateChanged;
            State.History.CollectionChanged -= OnHistoryChanged;
        };
    }

    private void OnHistoryChanged(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e) => UpdateList();

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppState.Run)) UpdateRun();
    }

    private void UpdateList()
    {
        List.ItemsSource = State.History
            .Select(e => new HistoryRow(e.Id, e.Title, $"{Strings.Date(e.CreatedMs)} · {Strings.Time(e.DurationMs)}",
                Strings.Get("Rename"), Strings.Get("Delete")))
            .ToList();
        Empty.Visibility = State.History.Count == 0 && State.Run is null ? Visibility.Visible : Visibility.Collapsed;
    }

    private void UpdateRun()
    {
        var run = State.Run;
        RunningCard.Visibility = run is null ? Visibility.Collapsed : Visibility.Visible;
        // One job at a time; the next file waits until it ends.
        OpenButton.IsEnabled = run is null;
        if (run is null) return;
        RunningName.Text = run.Name;
        RunningStatus.Text = Strings.Format("NotificationProgress", Strings.Step(run.Step), (int)(run.Fraction * 100));
    }

    private void OnOpen(object sender, RoutedEventArgs e) => _ = State.OpenFileAsync();

    private void OnVerify(object sender, RoutedEventArgs e) => _ = State.VerifyModelsAsync();

    private async void OnAbout(object sender, RoutedEventArgs e)
    {
        await new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = Strings.Get("AppName"),
            Content = new TextBlock { Text = AppState.Version, IsTextSelectionEnabled = true },
            CloseButtonText = Strings.Get("Ok"),
        }.ShowAsync();
    }

    private void OnRunning(object sender, RoutedEventArgs e) => State.ShowRunning();

    private void OnItemClick(object sender, ItemClickEventArgs e) => State.ShowResult(((HistoryRow)e.ClickedItem).Id);

    private async void OnRename(object sender, RoutedEventArgs e)
    {
        if (State.Entry((string)((FrameworkElement)sender).Tag) is not { } entry) return;
        var box = new TextBox { Text = entry.Title, Header = Strings.Get("Title") };
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = Strings.Get("Rename"),
            Content = box,
            PrimaryButtonText = Strings.Get("Save"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) State.Rename(entry, box.Text);
    }

    private async void OnDelete(object sender, RoutedEventArgs e)
    {
        if (State.Entry((string)((FrameworkElement)sender).Tag) is not { } entry) return;
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Content = Strings.Format("DeleteConfirm", entry.Title),
            PrimaryButtonText = Strings.Get("Delete"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Close,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) State.Delete(entry.Id);
    }
}
