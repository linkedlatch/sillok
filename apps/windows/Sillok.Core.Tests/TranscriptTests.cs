namespace Sillok.Tests;

public class TranscriptTests
{
    [Fact]
    public void FindsThePlayingSegment()
    {
        var transcript = new Transcript(LibraryTests.Document);
        Assert.Equal(9000, transcript.DurationMs);
        Assert.Equal(0, transcript.SegmentAt(1000));
        Assert.Equal(-1, transcript.SegmentAt(2200));  // between segments
        Assert.Equal(1, transcript.SegmentAt(2500));
        Assert.Equal(2, transcript.SegmentAt(8999));
        Assert.Equal(-1, transcript.SegmentAt(9000));
    }

    [Fact]
    public void ClearedSpeakerNameBecomesNullAndOtherFieldsSurvive()
    {
        var named = new Transcript(LibraryTests.Document).WithSpeakerNames(new Dictionary<int, string> { [1] = "Alex" });
        Assert.Equal("Alex", named.SpeakerName(1));
        var cleared = named.WithSpeakerNames(new Dictionary<int, string> { [1] = "  " });
        Assert.Null(cleared.SpeakerName(1));
        Assert.Contains("\"confidence\"", cleared.Json, StringComparison.Ordinal);
    }

    [Fact]
    public void ReadsTheModelsUsed()
    {
        var model = new Transcript(LibraryTests.Document).Models["transcription"];
        Assert.Equal("CPU", model.Device);
        Assert.True(model.FellBackToCpu);
    }
}
