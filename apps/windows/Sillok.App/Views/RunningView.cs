using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sillok.App.Pages;

namespace Sillok.App.Views;

/// <summary>The running file: overall progress, time left, and the text transcribed so far.</summary>
public sealed partial class RunningView : UserControl
{
    private static AppState State => App.State;
    private readonly TextBlock _name = new() { Style = Theme.Style("SubtitleTextBlockStyle"), TextTrimming = TextTrimming.CharacterEllipsis };
    private readonly ProgressBar _bar = new() { Maximum = 1 };
    private readonly TextBlock _step = new();
    private readonly TextBlock _left = new() { Style = Theme.Style("CaptionTextBlockStyle") };
    private readonly Button _cancel = new();
    private readonly TextBlock _details = new() { TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true, Style = Theme.Style("CaptionTextBlockStyle") };
    private readonly ListView _previews = new() { SelectionMode = ListViewSelectionMode.None };
    private readonly DispatcherTimer _clock = new() { Interval = TimeSpan.FromSeconds(1) };
    private int _shown;

    public RunningView()
    {
        _cancel.Click += (_, _) => State.Cancel();
        var header = new StackPanel
        {
            Spacing = 8,
            Padding = new Thickness(24, 20, 24, 12),
            Children =
            {
                _name, _bar, _step, _left,
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { _cancel } },
                new Expander { Header = Strings.Get("RunningDetails"), Content = _details, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch },
                new TextBlock { Text = Strings.Get("RunningPreviews"), Style = Theme.Style("CaptionTextBlockStyle"), TextWrapping = TextWrapping.Wrap },
            },
        };
        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.Children.Add(header);
        Grid.SetRow(_previews, 1);
        _previews.Padding = new Thickness(12, 0, 12, 12);
        root.Children.Add(_previews);
        Content = root;
        _clock.Tick += (_, _) => Update();
        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            _clock.Start();
            Update();
        };
        Unloaded += (_, _) =>
        {
            State.PropertyChanged -= OnStateChanged;
            _clock.Stop();
        };
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.Run) or nameof(AppState.Previews)) Update();
    }

    private void Update()
    {
        if (State.Run is not { } run) return;
        _name.Text = run.Name;
        _bar.Value = run.Overall;
        _step.Text = Strings.Format("NotificationProgress", Strings.Step(run.Step), (int)(run.Overall * 100));
        _left.Text = run.RemainingMs(State.Now) is { } left ? Strings.Format("RunningLeft", Strings.Time(left)) : "";
        _cancel.Content = Strings.Get(run.Cancelling ? "RunningCancelling" : "Cancel");
        _cancel.IsEnabled = !run.Cancelling;
        _details.Text = Strings.Get("RunningBackground") + (run.Read is { } read
            ? "\n" + Strings.Format("InfoStep", Strings.Step(RunStep.Read), Strings.Time(read.TotalMs)) +
              "\n" + Strings.Format("InfoReadParts", Strings.Time(read.DecodeMs), Strings.Time(read.PushMs), Strings.Time(read.CopyMs))
            : "");
        // New text is added at the end, and the list follows it.
        var previews = State.Previews;
        if (previews.Count < _shown)
        {
            _previews.Items.Clear();
            _shown = 0;
        }
        for (; _shown < previews.Count; _shown++)
        {
            var segment = previews[_shown];
            _previews.Items.Add(new StackPanel
            {
                Padding = new Thickness(0, 4, 0, 4),
                Children =
                {
                    new TextBlock { Text = Strings.Time(segment.StartMs), Style = Theme.Style("CaptionTextBlockStyle") },
                    new TextBlock { Text = segment.Text, TextWrapping = TextWrapping.Wrap },
                },
            });
        }
        if (_previews.Items.Count > 0) _previews.ScrollIntoView(_previews.Items[^1]);
    }
}

/// <summary>A file waiting in line, or one that failed: its place or its reason, and what to do.</summary>
public sealed partial class QueueView : UserControl
{
    private static AppState State => App.State;

    public QueueView(string key)
    {
        Key = key;
        var item = State.Queue.First(q => q.Key == key);
        var model = State.Catalog.FirstOrDefault(m => m.Id == item.Settings.TranscriptionModel) ?? State.RecommendedTranscription;
        var position = State.Queue.Where(q => q.Error is null).ToList().FindIndex(q => q.Key == key) + (State.Run is null ? 1 : 0);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        if (item.Error is not null)
        {
            var retry = new Button { Content = Strings.Get("Retry"), Style = Theme.Style("AccentButtonStyle") };
            retry.Click += (_, _) => State.Retry(key);
            actions.Children.Add(retry);
        }
        var remove = new Button { Content = Strings.Get("Remove") };
        remove.Click += (_, _) => State.Remove(key);
        actions.Children.Add(remove);
        Content = new StackPanel
        {
            Spacing = 12,
            Padding = new Thickness(24, 20, 24, 24),
            Children =
            {
                new TextBlock { Text = item.Name, Style = Theme.Style("SubtitleTextBlockStyle"), TextTrimming = TextTrimming.CharacterEllipsis },
                item.Error is not null
                    ? new InfoBar { Severity = InfoBarSeverity.Error, IsOpen = true, IsClosable = false, Message = item.Error }
                    : (UIElement)new TextBlock { Text = Strings.Format("DetailWaiting", Math.Max(position, 1)) },
                new TextBlock { Text = Strings.SettingsSummary(item.Settings, model), Style = Theme.Style("CaptionTextBlockStyle") },
                actions,
            },
        };
    }

    public string Key { get; }
}
