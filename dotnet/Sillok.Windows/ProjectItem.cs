using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Microsoft.UI.Xaml;
using Sillok.Core;

namespace Sillok.Windows;

public sealed class ProjectItem : INotifyPropertyChanged
{
    private TranscriptProject project;
    private TranscribeProgress? progress;
    private SegmentRow? playingRow;

    public ProjectItem(TranscriptProject project)
    {
        this.project = project;
        ReplaceRows(project.Segments);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public event Action<SegmentRow>? PlayingRowChanged;

    public Guid Id => project.Id;
    public string Name => project.SourceName;
    public ObservableCollection<SegmentRow> Rows { get; } = [];

    public TranscriptProject Project
    {
        get => project;
        set
        {
            project = value;
            Notify(nameof(StatusText));
        }
    }

    public TranscribeProgress? Progress
    {
        get => progress;
        set
        {
            progress = value;
            Notify();
            Notify(nameof(StatusText));
            Notify(nameof(ProgressValue));
            Notify(nameof(ProgressVisibility));
        }
    }

    public double ProgressValue => (progress?.FractionCompleted ?? 0) * 100;
    public Visibility ProgressVisibility => progress is null ? Visibility.Collapsed : Visibility.Visible;

    public string StatusText => progress switch
    {
        { Phase: TranscribePhase.Analyzing } => "음성 구간 분석 중",
        { Phase: TranscribePhase.Diarizing } => "화자 구분 중",
        { } running => $"전사 중 {running.FractionCompleted * 100:F0}%",
        null => project.Status switch
        {
            TranscriptStatus.Pending => "대기 중",
            TranscriptStatus.Transcribing => "이어서 전사 대기 중",
            TranscriptStatus.Completed => $"완료 · {TimeFormat.Clock(project.Duration ?? 0)}",
            TranscriptStatus.Cancelled => "취소됨",
            _ => project.FailureMessage ?? "실패",
        },
    };

    public void AppendRows(IEnumerable<Segment> segments)
    {
        foreach (var segment in segments) Rows.Add(new SegmentRow(segment, false));
    }

    public void ReplaceRows(IReadOnlyList<Segment> segments)
    {
        var showSpeakers = segments.Select(segment => segment.Speaker).Where(speaker => speaker is not null).Distinct().Count() > 1;
        playingRow = null;
        Rows.Clear();
        foreach (var segment in segments) Rows.Add(new SegmentRow(segment, showSpeakers));
    }

    public void UpdatePlaying(double? time)
    {
        SegmentRow? row = null;
        if (time is { } seconds)
        {
            int low = 0, high = Rows.Count;
            while (low < high)
            {
                var middle = (low + high) / 2;
                if (Rows[middle].Segment.Start <= seconds) low = middle + 1;
                else high = middle;
            }
            if (low > 0) row = Rows[low - 1];
        }
        if (ReferenceEquals(row, playingRow)) return;
        if (playingRow is not null) playingRow.IsPlaying = false;
        playingRow = row;
        if (row is null) return;
        row.IsPlaying = true;
        PlayingRowChanged?.Invoke(row);
    }

    private void Notify([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed class SegmentRow(Segment segment, bool showSpeaker) : INotifyPropertyChanged
{
    private bool isPlaying;

    public event PropertyChangedEventHandler? PropertyChanged;

    public Segment Segment => segment;
    public bool ShowSpeaker => showSpeaker;

    public bool IsPlaying
    {
        get => isPlaying;
        set
        {
            if (isPlaying == value) return;
            isPlaying = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(HighlightOpacity)));
        }
    }

    public double HighlightOpacity => isPlaying ? 0.2 : 0;
    public string TimeText => TimeFormat.Clock(Segment.Start);
    public string Text => Segment.Text;
    public string Language => Segment.Language.ToUpperInvariant();
    public string SpeakerText => ShowSpeaker && Segment.Speaker is { } speaker ? $"화자 {speaker}" : "";
    public Visibility SpeakerVisibility => SpeakerText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
    public double TextOpacity => Segment.IsLowConfidence ? 0.45 : 1;
    public Visibility LowConfidenceVisibility => Segment.IsLowConfidence ? Visibility.Visible : Visibility.Collapsed;
}

public static class TimeFormat
{
    public static string Clock(double seconds)
    {
        var total = (int)Math.Max(seconds, 0);
        return total >= 3600 ? $"{total / 3600}:{total / 60 % 60:D2}:{total % 60:D2}" : $"{total / 60:D2}:{total % 60:D2}";
    }
}
