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

    public ProjectItem(TranscriptProject project)
    {
        this.project = project;
        ReplaceRows(project.Segments);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

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
        Rows.Clear();
        foreach (var segment in segments) Rows.Add(new SegmentRow(segment, showSpeakers));
    }

    private void Notify([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed record SegmentRow(Segment Segment, bool ShowSpeaker)
{
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
