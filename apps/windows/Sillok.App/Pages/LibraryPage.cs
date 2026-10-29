using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Sillok.App.Views;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.System;

namespace Sillok.App.Pages;

/// <summary>
/// The main page, as Windows 11's own list-and-content apps lay it out: the
/// library on the left, the selected item on the right, a status bar below.
/// Files come in through the command bar (Ctrl+O) or by dropping them on the
/// window; items have context menus and keyboard shortcuts (F2, Delete).
/// </summary>
public sealed partial class LibraryPage : Page
{
    private static AppState State => App.State;
    private readonly ListView _list = new() { SelectionMode = ListViewSelectionMode.Single };
    private readonly ContentControl _detail = new()
    {
        HorizontalContentAlignment = HorizontalAlignment.Stretch,
        VerticalContentAlignment = VerticalAlignment.Stretch,
    };
    private readonly TextBlock _status = new() { Margin = new Thickness(12, 4, 12, 6), Style = Theme.Style("CaptionTextBlockStyle") };
    private readonly TextBlock _empty = new()
    {
        Text = Strings.Get("HomeEmpty"),
        TextWrapping = TextWrapping.Wrap,
        TextAlignment = TextAlignment.Center,
        Margin = new Thickness(24),
        VerticalAlignment = VerticalAlignment.Center,
    };
    private readonly Dictionary<string, LibraryRow> _rows = [];
    private string? _selected;
    private bool _syncing;

    public LibraryPage()
    {
        var open = new AppBarButton { Icon = new SymbolIcon(Symbol.OpenFile), Label = Strings.Get("Add") };
        open.KeyboardAccelerators.Add(new KeyboardAccelerator { Key = VirtualKey.O, Modifiers = VirtualKeyModifiers.Control });
        open.Click += async (_, _) =>
        {
            if (await State.PickFilesAsync() is { } paths) await AddDialog.ShowAsync(XamlRoot, paths);
        };
        var settings = new AppBarButton { Icon = new SymbolIcon(Symbol.Setting), Label = Strings.Get("Settings") };
        settings.Click += (_, _) => State.ShowSettings = true;
        var commands = new CommandBar { DefaultLabelPosition = CommandBarDefaultLabelPosition.Right, HorizontalAlignment = HorizontalAlignment.Left };
        commands.PrimaryCommands.Add(open);
        commands.PrimaryCommands.Add(new AppBarSeparator());
        commands.PrimaryCommands.Add(settings);

        _list.SelectionChanged += (_, _) =>
        {
            if (_syncing) return;
            _selected = (_list.SelectedItem as ListViewItem)?.Tag as string;
            ShowDetail();
        };
        _list.ContextRequested += OnContextRequested;

        var listPane = new Grid();
        listPane.Children.Add(_list);
        listPane.Children.Add(_empty);

        var root = new Grid { AllowDrop = true };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(320) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetColumnSpan(commands, 2);
        root.Children.Add(commands);
        Grid.SetRow(listPane, 1);
        root.Children.Add(listPane);
        var detailPane = new Border
        {
            Child = _detail,
            BorderThickness = new Thickness(1, 1, 0, 0),
            CornerRadius = new CornerRadius(8, 0, 0, 0),
            BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
            Background = (Brush)Application.Current.Resources["LayerFillColorDefaultBrush"],
        };
        Grid.SetRow(detailPane, 1);
        Grid.SetColumn(detailPane, 1);
        root.Children.Add(detailPane);
        Grid.SetRow(_status, 2);
        Grid.SetColumnSpan(_status, 2);
        root.Children.Add(_status);
        root.DragOver += OnDragOver;
        root.Drop += OnDrop;
        Content = root;

        AddAccelerator(VirtualKey.F2, (_, _) => RenameSelected());
        AddAccelerator(VirtualKey.Delete, (_, _) => DeleteSelected());

        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            Sync();
        };
        Unloaded += (_, _) => State.PropertyChanged -= OnStateChanged;
    }

    private void AddAccelerator(VirtualKey key, Windows.Foundation.TypedEventHandler<KeyboardAccelerator, KeyboardAcceleratorInvokedEventArgs> handler)
    {
        var accelerator = new KeyboardAccelerator { Key = key };
        accelerator.Invoked += (sender, args) =>
        {
            // Not while a text box has the key: F2 and Delete belong to it then.
            if (FocusManager.GetFocusedElement(XamlRoot) is TextBox) return;
            args.Handled = true;
            handler(sender, args);
        };
        KeyboardAccelerators.Add(accelerator);
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(AppState.Library):
                Sync();
                break;
            case nameof(AppState.Finished) when State.Finished is { } finished && finished.Key == _selected:
                // The running file's page becomes its transcript.
                _selected = finished.Id;
                Sync();
                break;
        }
    }

    /// <summary>Brings the list in line with the library, keeping the selection and the rows' identity.</summary>
    private void Sync()
    {
        _syncing = true;
        var items = State.Library;
        var keys = items.Select(i => i.Key).ToHashSet();
        foreach (var gone in _rows.Keys.Where(k => !keys.Contains(k)).ToList())
        {
            _list.Items.Remove(_rows[gone].Item);
            _rows.Remove(gone);
        }
        for (var i = 0; i < items.Count; i++)
        {
            if (!_rows.TryGetValue(items[i].Key, out var row))
            {
                row = new LibraryRow(items[i].Key);
                _rows[items[i].Key] = row;
            }
            row.Show(items[i], State.Now);
            var at = _list.Items.IndexOf(row.Item);
            if (at != i)
            {
                if (at >= 0) _list.Items.RemoveAt(at);
                _list.Items.Insert(i, row.Item);
            }
        }
        _empty.Visibility = items.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        if (_selected is not null && !keys.Contains(_selected)) _selected = null;  // cancelled, removed, deleted
        _list.SelectedItem = _selected is not null ? _rows[_selected].Item : null;
        _syncing = false;
        ShowDetail();
    }

    private void ShowDetail()
    {
        var item = State.Library.FirstOrDefault(i => i.Key == _selected);
        switch (item)
        {
            case LibraryItem.Running:
                if (_detail.Content is not RunningView) _detail.Content = new RunningView();
                break;
            case LibraryItem.Waiting or LibraryItem.Failed:
                if ((_detail.Content as QueueView)?.Key != item.Key) _detail.Content = new QueueView(item.Key);
                break;
            case LibraryItem.Done done:
                if ((_detail.Content as TranscriptView)?.Id != done.Entry.Id)
                {
                    var view = new TranscriptView(done.Entry);
                    view.StatusChanged += (_, text) => _status.Text = text;
                    _detail.Content = view;
                }
                break;
            default:
                _detail.Content = new TextBlock
                {
                    Text = Strings.Get("DetailNone"),
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center,
                    Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"],
                };
                _status.Text = "";
                break;
        }
    }

    // --- Context menu and shortcuts ------------------------------------------

    private void OnContextRequested(UIElement sender, ContextRequestedEventArgs args)
    {
        var container = (args.OriginalSource as DependencyObject) is { } source ? FindItem(source) : null;
        if (container?.Tag is not string key) return;
        _list.SelectedItem = container;
        var menu = new MenuFlyout();
        switch (State.Library.FirstOrDefault(i => i.Key == key))
        {
            case LibraryItem.Done done:
                menu.Items.Add(MenuItem("Rename", Symbol.Rename, VirtualKey.F2, RenameSelected));
                menu.Items.Add(MenuItem("OpenFolder", Symbol.Folder, null, () => OpenFolder(done.Entry.Id)));
                menu.Items.Add(new MenuFlyoutSeparator());
                menu.Items.Add(MenuItem("Delete", Symbol.Delete, VirtualKey.Delete, DeleteSelected));
                break;
            case LibraryItem.Failed failed:
                menu.Items.Add(MenuItem("Retry", Symbol.Refresh, null, () => State.Retry(failed.Key)));
                menu.Items.Add(MenuItem("Remove", Symbol.Cancel, VirtualKey.Delete, DeleteSelected));
                break;
            case LibraryItem.Waiting:
                menu.Items.Add(MenuItem("Remove", Symbol.Cancel, VirtualKey.Delete, DeleteSelected));
                break;
            case LibraryItem.Running:
                menu.Items.Add(MenuItem("Cancel", Symbol.Cancel, null, State.Cancel));
                break;
            default:
                return;
        }
        if (args.TryGetPosition(container, out var point)) menu.ShowAt(container, point);
        else menu.ShowAt(container);
        args.Handled = true;
    }

    private static ListViewItem? FindItem(DependencyObject element)
    {
        for (var e = element; e is not null; e = VisualTreeHelper.GetParent(e))
        {
            if (e is ListViewItem item) return item;
        }
        return null;
    }

    private static MenuFlyoutItem MenuItem(string text, Symbol icon, VirtualKey? key, Action action)
    {
        var item = new MenuFlyoutItem { Text = Strings.Get(text), Icon = new SymbolIcon(icon) };
        if (key is { } shortcut) item.KeyboardAcceleratorTextOverride = shortcut == VirtualKey.Delete ? "Del" : shortcut.ToString();
        item.Click += (_, _) => action();
        return item;
    }

    private void RenameSelected()
    {
        if (State.Library.FirstOrDefault(i => i.Key == _selected) is LibraryItem.Done done) _ = Dialogs.RenameAsync(XamlRoot, done.Entry);
    }

    private void DeleteSelected()
    {
        switch (State.Library.FirstOrDefault(i => i.Key == _selected))
        {
            case LibraryItem.Done done:
                _ = Dialogs.DeleteAsync(XamlRoot, done.Entry);
                break;
            case LibraryItem.Waiting or LibraryItem.Failed:
                State.Remove(_selected!);
                break;
        }
    }

    internal static void OpenFolder(string id) =>
        System.Diagnostics.Process.Start("explorer.exe", $"/select,\"{State.Store.TranscriptPath(id)}\"");

    // --- Dropping files -----------------------------------------------------------

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        e.AcceptedOperation = DataPackageOperation.Copy;
        e.DragUIOverride.Caption = Strings.Get("DropCaption");
    }

    private async void OnDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        var paths = (await e.DataView.GetStorageItemsAsync()).OfType<StorageFile>().Select(f => f.Path).ToList();
        if (paths.Count > 0) await AddDialog.ShowAsync(XamlRoot, paths);
    }
}

/// <summary>A row of the library list, updated in place so that the list keeps its selection and scroll.</summary>
internal sealed class LibraryRow
{
    private readonly TextBlock _title = new() { TextTrimming = TextTrimming.CharacterEllipsis, Style = Theme.Style("BodyStrongTextBlockStyle") };
    private readonly TextBlock _detail = new() { TextTrimming = TextTrimming.CharacterEllipsis, Style = Theme.Style("CaptionTextBlockStyle") };
    private readonly ProgressBar _progress = new() { Maximum = 1, Margin = new Thickness(0, 4, 0, 0) };
    private readonly FontIcon _warning = new() { Glyph = "", FontSize = 14, Margin = new Thickness(0, 0, 8, 0) };

    public LibraryRow(string key)
    {
        var text = new StackPanel { Children = { _title, _detail, _progress } };
        var row = new StackPanel { Orientation = Orientation.Horizontal, Padding = new Thickness(0, 6, 0, 6), Children = { _warning, text } };
        Item = new ListViewItem { Content = row, Tag = key };
    }

    public ListViewItem Item { get; }

    public void Show(LibraryItem item, long now)
    {
        _progress.Visibility = Visibility.Collapsed;
        _warning.Visibility = Visibility.Collapsed;
        switch (item)
        {
            case LibraryItem.Running running:
                var run = running.Run;
                _title.Text = run.Name;
                var percent = (int)(run.Overall * 100);
                _detail.Text = run.RemainingMs(now) is { } left
                    ? Strings.Format("LibraryRunningLeft", Strings.Step(run.Step), percent, Strings.Time(left))
                    : Strings.Format("NotificationProgress", Strings.Step(run.Step), percent);
                _progress.Value = run.Overall;
                _progress.Visibility = Visibility.Visible;
                break;
            case LibraryItem.Waiting waiting:
                _title.Text = waiting.Item.Name;
                _detail.Text = Strings.Format("LibraryWaiting", waiting.Position);
                break;
            case LibraryItem.Failed failed:
                _title.Text = failed.Item.Name;
                _detail.Text = Strings.Format("LibraryFailed", failed.Item.Error);
                _warning.Visibility = Visibility.Visible;
                break;
            case LibraryItem.Done done:
                _title.Text = done.Entry.Title;
                _detail.Text = $"{Strings.Date(done.Entry.CreatedMs)} · {Strings.Time(done.Entry.DurationMs)}";
                break;
        }
    }
}
