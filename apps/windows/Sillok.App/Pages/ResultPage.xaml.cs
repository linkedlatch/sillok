using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Media.Core;
using Windows.Media.Playback;

namespace Sillok.App.Pages;

/// <summary>A sentence of the transcript as listed: speaker and time, then the text.</summary>
public sealed record SegmentRow(string Header, string Text);

/// <summary>
/// The transcript: segments play from a second before them (include/sillok/sillok.h),
/// the playing one is selected, speakers can be named, and the text exported.
/// </summary>
public sealed partial class ResultPage : Page
{
    private static AppState State => App.State;
    private readonly DispatcherTimer _clock = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private readonly string _id;
    private JobEntry? _entry;
    private Transcript? _transcript;
    private MediaPlayer? _player;
    private bool _updatingSeek;

    public ResultPage()
    {
        InitializeComponent();
        _id = State.ResultId ?? "";
        BackButton.Label = Strings.Get("Back");
        SpeakersButton.Label = Strings.Get("Speakers");
        ExportButton.Label = Strings.Get("Export");
        ExportTxt.Text = Strings.Get("ExportTxt");
        ExportSrt.Text = Strings.Get("ExportSrt");
        ExportJson.Text = Strings.Get("ExportJson");
        InfoButton.Label = Strings.Get("Info");
        _clock.Tick += (_, _) => UpdatePlayback();
        Loaded += (_, _) => Load();
        Unloaded += (_, _) =>
        {
            _clock.Stop();
            _player?.Dispose();
            _player = null;
        };
    }

    private void Load()
    {
        _entry = State.Entry(_id);
        _transcript = State.LoadTranscript(_id);
        Heading.Text = _entry?.Title ?? "";
        ShowSegments();

        var audio = State.Store.AudioPath(_id);
        if (File.Exists(audio))
        {
            _player = new MediaPlayer { Source = MediaSource.CreateFromUri(new Uri(audio)) };
            _clock.Start();
        }
        Player.Visibility = _player is null ? Visibility.Collapsed : Visibility.Visible;
    }

    private void ShowSegments()
    {
        var transcript = _transcript;
        Message.Text = transcript is null ? Strings.Get("ResultMissing")
            : transcript.Segments.Count == 0 ? Strings.Get("ResultEmpty") : "";
        SpeakersButton.Visibility = transcript?.Speakers.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        if (transcript is null) return;
        Segments.ItemsSource = transcript.Segments.Select(s =>
        {
            var time = Strings.Time(s.StartMs);
            return new SegmentRow(s.Speaker is { } id ? $"{SpeakerName(transcript, id)} · {time}" : time, s.Text);
        }).ToList();
    }

    private static string SpeakerName(Transcript transcript, int id) =>
        transcript.SpeakerName(id) ?? Strings.Get("SpeakerLabel").Replace("{n}", (id + 1).ToString(System.Globalization.CultureInfo.CurrentCulture), StringComparison.Ordinal);

    private void UpdatePlayback()
    {
        if (_player is null || _transcript is null) return;
        var session = _player.PlaybackSession;
        var position = (long)session.Position.TotalMilliseconds;
        var length = (long)session.NaturalDuration.TotalMilliseconds;
        PlayIcon.Symbol = session.PlaybackState == MediaPlaybackState.Playing ? Symbol.Pause : Symbol.Play;
        Position.Text = Strings.Time(position);
        Length.Text = Strings.Time(length);
        _updatingSeek = true;
        Seek.Maximum = Math.Max(length, 1);
        Seek.Value = position;
        _updatingSeek = false;
        if (session.PlaybackState != MediaPlaybackState.Playing) return;
        var index = _transcript.SegmentAt(position);
        if (index >= 0 && Segments.SelectedIndex != index)
        {
            Segments.SelectedIndex = index;
            Segments.ScrollIntoView(Segments.SelectedItem);
        }
    }

    private void OnSegmentClick(object sender, ItemClickEventArgs e)
    {
        if (_player is null || _transcript is null) return;
        var index = ((List<SegmentRow>)Segments.ItemsSource).IndexOf((SegmentRow)e.ClickedItem);
        if (index < 0) return;
        _player.PlaybackSession.Position = TimeSpan.FromMilliseconds(Math.Max(0, _transcript.Segments[index].StartMs - 1000));
        _player.Play();
    }

    private void OnPlayPause(object sender, RoutedEventArgs e)
    {
        if (_player is null) return;
        if (_player.PlaybackSession.PlaybackState == MediaPlaybackState.Playing) _player.Pause();
        else _player.Play();
    }

    private void OnSeek(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_updatingSeek || _player is null) return;
        _player.PlaybackSession.Position = TimeSpan.FromMilliseconds(e.NewValue);
    }

    private void OnBack(object sender, RoutedEventArgs e) => State.GoHome();

    private async void OnExport(object sender, RoutedEventArgs e)
    {
        if (_entry is null || _transcript is null) return;
        await State.ExportAsync(_entry, _transcript, (string)((FrameworkElement)sender).Tag);
    }

    private async void OnSpeakers(object sender, RoutedEventArgs e)
    {
        if (_transcript is not { } transcript) return;
        var boxes = transcript.Speakers.ToDictionary(s => s.Id, s => new TextBox
        {
            Header = Strings.Get("SpeakerLabel").Replace("{n}", (s.Id + 1).ToString(System.Globalization.CultureInfo.CurrentCulture), StringComparison.Ordinal),
            Text = s.Name ?? "",
        });
        var panel = new StackPanel { Spacing = 8 };
        foreach (var box in boxes.Values) panel.Children.Add(box);
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = Strings.Get("Speakers"),
            Content = new ScrollViewer { Content = panel },
            PrimaryButtonText = Strings.Get("Save"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
        _transcript = State.SaveSpeakerNames(_id, transcript, boxes.ToDictionary(b => b.Key, b => b.Value.Text));
        ShowSegments();
    }

    /// <summary>Where the time went, to compare devices and settings.</summary>
    private async void OnInfo(object sender, RoutedEventArgs e)
    {
        if (_entry is null) return;
        var lines = new List<string> { Strings.Format("InfoLength", Strings.Time(_entry.DurationMs)) };
        if (_entry.Stats is { } stats)
        {
            long total = 0;
            foreach (var step in Enum.GetValues<RunStep>())
            {
                if (!stats.TimingsMs.TryGetValue(step.ToString().ToLowerInvariant(), out var ms)) continue;
                total += ms;
                lines.Add(Strings.Format("InfoStep", Strings.Step(step), Strings.Time(ms)));
                if (step == RunStep.Read && stats.TimingsMs.TryGetValue("read_decode", out var decode))
                {
                    lines.Add(Strings.Format("InfoReadParts", Strings.Time(decode),
                        Strings.Time(stats.TimingsMs.GetValueOrDefault("read_push")), Strings.Time(stats.TimingsMs.GetValueOrDefault("read_copy"))));
                }
            }
            lines.Add(Strings.Format("InfoTotal", Strings.Time(total), total / (double)Math.Max(_entry.DurationMs, 1)));
        }
        if (_transcript?.Models.GetValueOrDefault("transcription") is { } model)
        {
            lines.Add(Strings.Format("InfoDevice", model.FellBackToCpu ? Strings.Format("InfoFellBack", model.Device) : model.Device));
        }
        if (_entry.Stats is { } s) lines.Add(Strings.Format("InfoThreads", s.Threads));
        await new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = Strings.Get("Info"),
            Content = new TextBlock { Text = string.Join("\n", lines), IsTextSelectionEnabled = true },
            CloseButtonText = Strings.Get("Ok"),
        }.ShowAsync();
    }
}
