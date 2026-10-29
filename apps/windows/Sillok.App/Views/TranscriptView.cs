using System.Globalization;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Sillok.App.Pages;
using Windows.ApplicationModel.DataTransfer;
using Windows.Media.Core;
using Windows.Media.Playback;
using Windows.System;
using Windows.UI.Text;

namespace Sillok.App.Views;

/// <summary>
/// A transcript, read as paragraphs by speaker. Each sentence is a link:
/// clicking it plays it from a second before (include/sillok/sillok.h) and
/// puts its exact times in the status bar; right-clicking offers to play or
/// copy it. The system's media controls play the listening copy, and the
/// playing sentence stands out and can be followed.
/// </summary>
public sealed partial class TranscriptView : UserControl
{
    private static AppState State => App.State;
    private readonly JobEntry _entry;
    private Transcript? _transcript;
    private readonly ListView _paragraphs = new() { SelectionMode = ListViewSelectionMode.None, Padding = new Thickness(12, 0, 12, 12) };
    private readonly AutoSuggestBox _search = new() { QueryIcon = new SymbolIcon(Symbol.Find), Width = 240, PlaceholderText = Strings.Get("SearchHint") };
    private readonly AppBarToggleButton _follow = new() { Icon = new SymbolIcon(Symbol.Sync), Label = Strings.Get("Follow"), IsChecked = true };
    private readonly MediaPlayerElement _player = new() { AreTransportControlsEnabled = true, Height = 110 };
    private readonly DispatcherTimer _clock = new() { Interval = TimeSpan.FromMilliseconds(150) };
    // Sentence i's run, and the paragraph item that holds it.
    private readonly Dictionary<int, Run> _runs = [];
    private readonly Dictionary<Run, int> _sentences = [];
    private readonly Dictionary<int, FrameworkElement> _items = [];
    private int _playing = -1;
    private int _selected = -1;

    public TranscriptView(JobEntry entry)
    {
        _entry = entry;
        Id = entry.Id;
        _transcript = State.LoadTranscript(entry.Id);

        var title = new TextBlock
        {
            Text = entry.Title, Style = Theme.Style("SubtitleTextBlockStyle"),
            TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
        };
        var commands = new CommandBar { DefaultLabelPosition = CommandBarDefaultLabelPosition.Right };
        commands.PrimaryCommands.Add(_follow);
        if (_transcript?.Speakers.Count > 0)
        {
            var speakers = new AppBarButton { Icon = new SymbolIcon(Symbol.People), Label = Strings.Get("Speakers") };
            speakers.Click += (_, _) => _ = NameSpeakersAsync();
            commands.PrimaryCommands.Add(speakers);
        }
        var export = new MenuFlyout();
        foreach (var (format, text) in new[] { ("txt", "ExportTxt"), ("srt", "ExportSrt"), ("json", "ExportJson") })
        {
            var item = new MenuFlyoutItem { Text = Strings.Get(text) };
            item.Click += (_, _) => { if (_transcript is not null) _ = State.ExportAsync(_entry, _transcript, format); };
            export.Items.Add(item);
        }
        var recording = new MenuFlyoutItem { Text = Strings.Get("ExportRecording") };
        recording.Click += (_, _) => _ = State.ExportRecordingAsync(_entry);
        export.Items.Add(new MenuFlyoutSeparator());
        export.Items.Add(recording);
        commands.PrimaryCommands.Add(new AppBarButton { Icon = new SymbolIcon(Symbol.Save), Label = Strings.Get("Export"), Flyout = export });
        commands.SecondaryCommands.Add(Command("Rename", Symbol.Rename, () => _ = Dialogs.RenameAsync(XamlRoot, _entry)));
        commands.SecondaryCommands.Add(Command("OpenFolder", Symbol.Folder, () => LibraryPage.OpenFolder(_entry.Id)));
        commands.SecondaryCommands.Add(Command("Info", Symbol.Help, () => _ = ShowInfoAsync()));
        commands.SecondaryCommands.Add(new AppBarSeparator());
        commands.SecondaryCommands.Add(Command("Delete", Symbol.Delete, () => _ = Dialogs.DeleteAsync(XamlRoot, _entry)));

        var header = new Grid { Padding = new Thickness(24, 12, 12, 4), ColumnSpacing = 12 };
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        header.Children.Add(title);
        _search.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(_search, 1);
        header.Children.Add(_search);
        Grid.SetColumn(commands, 2);
        header.Children.Add(commands);
        _search.TextChanged += (_, _) => ShowParagraphs();

        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.Children.Add(header);
        Grid.SetRow(_paragraphs, 1);
        root.Children.Add(_paragraphs);
        Grid.SetRow(_player, 2);
        root.Children.Add(_player);
        Content = root;

        var audio = State.Store.AudioPath(entry.Id);
        if (File.Exists(audio))
        {
            _player.Source = MediaSource.CreateFromUri(new Uri(audio));
            _player.TransportControls.IsCompact = true;
            _player.TransportControls.IsZoomButtonVisible = false;
        }
        else
        {
            _player.Visibility = Visibility.Collapsed;
        }

        Accelerator(VirtualKey.F, VirtualKeyModifiers.Control, () => _search.Focus(FocusState.Keyboard));
        Accelerator(VirtualKey.Space, VirtualKeyModifiers.None, TogglePlayback);
        _clock.Tick += (_, _) => FollowPlayback();
        Loaded += (_, _) =>
        {
            ShowParagraphs();
            _clock.Start();
        };
        Unloaded += (_, _) =>
        {
            _clock.Stop();
            _player.MediaPlayer?.Pause();
            _player.SetMediaPlayer(null);
        };
    }

    public string Id { get; }

    /// <summary>Text for the window's status bar: the selected sentence's times, language and speaker.</summary>
    public event EventHandler<string>? StatusChanged;

    private static AppBarButton Command(string text, Symbol icon, Action action)
    {
        var button = new AppBarButton { Label = Strings.Get(text), Icon = new SymbolIcon(icon) };
        button.Click += (_, _) => action();
        return button;
    }

    private void Accelerator(VirtualKey key, VirtualKeyModifiers modifiers, Action action)
    {
        var accelerator = new KeyboardAccelerator { Key = key, Modifiers = modifiers };
        accelerator.Invoked += (_, args) =>
        {
            // Space belongs to text boxes and buttons when they have the focus.
            if (key == VirtualKey.Space && FocusManager.GetFocusedElement(XamlRoot) is TextBox or ButtonBase or AutoSuggestBox) return;
            args.Handled = true;
            action();
        };
        KeyboardAccelerators.Add(accelerator);
    }

    // --- Paragraphs ----------------------------------------------------------------

    /// <summary>Consecutive sentences of one speaker (or, without speakers, close together) read as a paragraph.</summary>
    private static List<List<int>> Paragraphs(Transcript transcript)
    {
        var result = new List<List<int>>();
        var current = new List<int>();
        for (var i = 0; i < transcript.Segments.Count; i++)
        {
            var segment = transcript.Segments[i];
            if (current.Count > 0)
            {
                var previous = transcript.Segments[current[^1]];
                if (previous.Speaker != segment.Speaker ||
                    (segment.Speaker is null && segment.StartMs - previous.EndMs > 2000) || current.Count >= 8)
                {
                    result.Add(current);
                    current = [];
                }
            }
            current.Add(i);
        }
        if (current.Count > 0) result.Add(current);
        return result;
    }

    private void ShowParagraphs()
    {
        _paragraphs.Items.Clear();
        _runs.Clear();
        _sentences.Clear();
        _items.Clear();
        if (_transcript is not { } transcript)
        {
            _paragraphs.Items.Add(new TextBlock { Text = Strings.Get("ResultMissing") });
            return;
        }
        if (transcript.Segments.Count == 0)
        {
            _paragraphs.Items.Add(new TextBlock { Text = Strings.Get("ResultEmpty") });
            return;
        }
        var query = _search.Text.Trim();
        foreach (var paragraph in Paragraphs(transcript))
        {
            var matches = query.Length == 0 ||
                          paragraph.Any(i => transcript.Segments[i].Text.Contains(query, StringComparison.CurrentCultureIgnoreCase));
            if (matches) _paragraphs.Items.Add(ParagraphView(transcript, paragraph, query));
        }
        if (_paragraphs.Items.Count == 0) _paragraphs.Items.Add(new TextBlock { Text = Strings.Get("SearchNone") });
        Restyle();
    }

    private StackPanel ParagraphView(Transcript transcript, List<int> paragraph, string query)
    {
        var first = transcript.Segments[paragraph[0]];
        var heading = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        if (first.Speaker is { } speaker)
        {
            var name = new HyperlinkButton { Content = SpeakerName(transcript, speaker), Padding = new Thickness(0) };
            name.Click += (_, _) => _ = NameSpeakersAsync();
            heading.Children.Add(name);
        }
        heading.Children.Add(new TextBlock
        {
            Text = Strings.Time(first.StartMs), VerticalAlignment = VerticalAlignment.Center,
            Style = Theme.Style("CaptionTextBlockStyle"),
        });

        var text = new Paragraph();
        foreach (var i in paragraph)
        {
            var run = new Run { Text = transcript.Segments[i].Text };
            if (query.Length > 0 && run.Text.Contains(query, StringComparison.CurrentCultureIgnoreCase))
            {
                run.FontWeight = FontWeights.Bold;
            }
            var link = new Hyperlink { UnderlineStyle = UnderlineStyle.None, Foreground = Brush("TextFillColorPrimaryBrush") };
            link.Inlines.Add(run);
            var index = i;
            link.Click += (_, _) => Select(index, play: true);
            if (text.Inlines.Count > 0) text.Inlines.Add(new Run { Text = " " });
            text.Inlines.Add(link);
            _runs[i] = run;
            _sentences[run] = i;
        }
        var block = new RichTextBlock { TextWrapping = TextWrapping.Wrap, FontSize = 15, LineHeight = 24 };
        block.Blocks.Add(text);
        block.ContextRequested += OnSentenceContext;

        var view = new StackPanel { Spacing = 2, Padding = new Thickness(12, 8, 12, 8), Children = { heading, block } };
        foreach (var i in paragraph) _items[i] = view;
        return view;
    }

    private static string SpeakerName(Transcript transcript, int id) =>
        transcript.SpeakerName(id) ?? Strings.Get("SpeakerLabel").Replace("{n}", (id + 1).ToString(CultureInfo.CurrentCulture), StringComparison.Ordinal);

    /// <summary>The right-clicked sentence: play it, or copy it.</summary>
    private void OnSentenceContext(UIElement sender, ContextRequestedEventArgs args)
    {
        if (sender is not RichTextBlock block || !args.TryGetPosition(block, out var point)) return;
        if (block.GetPositionFromPoint(point)?.Parent is not Run run || !_sentences.TryGetValue(run, out var index)) return;
        Select(index, play: false);
        var play = new MenuFlyoutItem { Text = Strings.Get("SegmentPlay"), Icon = new SymbolIcon(Symbol.Play) };
        play.Click += (_, _) => Select(index, play: true);
        var copy = new MenuFlyoutItem { Text = Strings.Get("SegmentCopy"), Icon = new SymbolIcon(Symbol.Copy) };
        copy.Click += (_, _) =>
        {
            var data = new DataPackage();
            data.SetText(_transcript!.Segments[index].Text);
            Clipboard.SetContent(data);
        };
        var menu = new MenuFlyout { Items = { play, copy } };
        menu.ShowAt(block, point);
        args.Handled = true;
    }

    // --- Playback --------------------------------------------------------------------

    private void Select(int index, bool play)
    {
        if (_transcript is not { } transcript) return;
        _selected = index;
        var segment = transcript.Segments[index];
        var language = segment.Language switch
        {
            "ko" => Strings.Get("LanguageNameKo"),
            "en" => Strings.Get("LanguageNameEn"),
            _ => segment.Language,
        };
        var parts = new List<string> { $"{Precise(segment.StartMs)} – {Precise(segment.EndMs)}", language };
        if (segment.Speaker is { } speaker) parts.Add(SpeakerName(transcript, speaker));
        StatusChanged?.Invoke(this, string.Join(" · ", parts));
        if (play && _player.MediaPlayer is { } player)
        {
            player.PlaybackSession.Position = TimeSpan.FromMilliseconds(Math.Max(0, segment.StartMs - 1000));
            player.Play();
        }
        Restyle();
    }

    /// <summary>"1:02:03.4": tenths of a second, for a selected sentence.</summary>
    private static string Precise(long ms) => string.Create(CultureInfo.InvariantCulture, $"{Strings.Time(ms)}.{ms % 1000 / 100}");

    private void TogglePlayback()
    {
        if (_player.MediaPlayer is not { } player) return;
        if (player.PlaybackSession.PlaybackState == MediaPlaybackState.Playing) player.Pause();
        else player.Play();
    }

    private void FollowPlayback()
    {
        if (_transcript is null || _player.MediaPlayer is not { } player) return;
        var session = player.PlaybackSession;
        var playing = session.PlaybackState == MediaPlaybackState.Playing
            ? _transcript.SegmentAt((long)session.Position.TotalMilliseconds)
            : -1;
        if (playing == _playing) return;
        _playing = playing;
        Restyle();
        if (playing >= 0 && _follow.IsChecked == true && _items.TryGetValue(playing, out var item)) _paragraphs.ScrollIntoView(item);
    }

    /// <summary>The playing sentence in the accent color, the selected one underlined.</summary>
    private void Restyle()
    {
        foreach (var (index, run) in _runs)
        {
            run.Foreground = index == _playing ? Brush("AccentTextFillColorPrimaryBrush") : null;
            run.TextDecorations = index == _selected ? TextDecorations.Underline : TextDecorations.None;
        }
    }

    private static Brush Brush(string key) => (Brush)Application.Current.Resources[key];

    // --- Dialogs ---------------------------------------------------------------------

    private async Task NameSpeakersAsync()
    {
        if (_transcript is not { } transcript) return;
        var boxes = new Dictionary<int, TextBox>();
        var panel = new StackPanel { Spacing = 8 };
        foreach (var speaker in transcript.Speakers)
        {
            var label = Strings.Get("SpeakerLabel").Replace("{n}", (speaker.Id + 1).ToString(CultureInfo.CurrentCulture), StringComparison.Ordinal);
            var box = new TextBox { Header = label, PlaceholderText = label, Text = speaker.Name ?? "", MinWidth = 240 };
            var listen = new Button { Content = new SymbolIcon(Symbol.Play), VerticalAlignment = VerticalAlignment.Bottom };
            ToolTipService.SetToolTip(listen, Strings.Get("SpeakerListen"));
            var id = speaker.Id;
            listen.Click += (_, _) =>
            {
                var first = transcript.Segments.ToList().FindIndex(s => s.Speaker == id);
                if (first >= 0) Select(first, play: true);
            };
            boxes[speaker.Id] = box;
            panel.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { box, listen } });
        }
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = Strings.Get("Speakers"),
            Content = new ScrollViewer { Content = panel },
            PrimaryButtonText = Strings.Get("Save"),
            CloseButtonText = Strings.Get("Cancel"),
            DefaultButton = ContentDialogButton.Primary,
        };
        var result = await dialog.ShowAsync();
        _player.MediaPlayer?.Pause();
        if (result != ContentDialogResult.Primary) return;
        _transcript = State.SaveSpeakerNames(_entry.Id, transcript, boxes.ToDictionary(b => b.Key, b => b.Value.Text));
        ShowParagraphs();
    }

    /// <summary>Where the time went, to compare devices and settings.</summary>
    private async Task ShowInfoAsync()
    {
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
            lines.Add(model.Name);
            lines.Add(Strings.Format("InfoDevice", model.FellBackToCpu ? Strings.Format("InfoFellBack", model.Device) : model.Device));
        }
        if (_entry.Stats is { } s) lines.Add(Strings.Format("InfoThreads", s.Threads));
        await Dialogs.MessageAsync(XamlRoot, Strings.Get("Info"), string.Join("\n", lines));
    }
}
