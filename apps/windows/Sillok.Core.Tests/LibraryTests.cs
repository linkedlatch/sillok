namespace Sillok.Tests;

public class LibraryTests
{
    internal const string Document = """
        {"version": 1, "duration_ms": 9000,
         "speakers": [{"id": 0, "name": null}, {"id": 1, "name": null}],
         "segments": [
           {"start_ms": 0, "end_ms": 2000, "text": "안녕하세요 😀", "language": "ko", "speaker": 0,
            "words": [{"text": "안녕하세요", "start_ms": 0, "end_ms": 900, "confidence": 0.9, "speaker": 0}]},
           {"start_ms": 2500, "end_ms": 4000, "text": "Hello", "language": "en", "speaker": 1, "words": []},
           {"start_ms": 5000, "end_ms": 9000, "text": "다시", "language": "ko", "speaker": 0, "words": []}],
         "models": {"transcription": {"name": "whisper", "device": "CPU", "fell_back_to_cpu": true}}}
        """;

    [Fact]
    public void ThisComputerRunsTheLibrary()
    {
        Assert.Empty(CpuSupport.Missing);
    }

    [Fact]
    public void CatalogNamesTheModels()
    {
        var roles = SillokLibrary.Catalog().Select(m => m.Role).ToList();
        Assert.Contains("voice_detection", roles);
        Assert.Contains("transcription", roles);
        Assert.Contains("diarization", roles);
        Assert.StartsWith("sillok ", SillokLibrary.Version, StringComparison.Ordinal);
    }

    [Fact]
    public void EachRoleHasOneRecommendedModel()
    {
        var models = SillokLibrary.EngineModels(null);
        Assert.Equal(["diarization", "transcription", "voice_detection"], models.Select(m => m.Role).Order());
        var other = SillokLibrary.Catalog().First(m => m.Role == "transcription" && !m.Recommended);
        Assert.Contains(SillokLibrary.EngineModels(other.Id), m => m.Id == other.Id);
    }

    [Fact]
    public void DevicesIncludeTheCpu() => Assert.Contains(SillokLibrary.Devices(), d => d.Type == "cpu");

    [Fact]
    public void EngineReportsMissingModels()
    {
        var empty = Directory.CreateTempSubdirectory("sillok-no-models").FullName;
        var error = Assert.Throws<SillokException>(() => new Engine(empty, Path.GetTempPath(), null).Dispose());
        Assert.True(error.IsModelError);
        Assert.Contains("missing", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ExportKeepsUnicodeAndNamesSpeakers()
    {
        var named = new Transcript(Document).WithSpeakerNames(new Dictionary<int, string> { [0] = "민지 " });
        var text = SillokLibrary.Export(named.Json, "txt", "화자 {n}");
        Assert.Contains("민지: 안녕하세요 😀", text, StringComparison.Ordinal);
        Assert.Contains("화자 2: Hello", text, StringComparison.Ordinal);
        Assert.Contains("00:00:02,500 --> 00:00:04,000", SillokLibrary.Export(named.Json, "srt", "Speaker {n}"), StringComparison.Ordinal);
    }

    [Fact]
    public void CheckReportsEveryCatalogFile()
    {
        var empty = Directory.CreateTempSubdirectory("sillok-check").FullName;
        var status = SillokLibrary.Check(empty, verifyHash: false);
        Assert.Equal(SillokLibrary.Catalog().SelectMany(m => m.Files).Select(f => f.File).Distinct().Count(), status.Count);
        Assert.All(status.Values, s => Assert.Equal("missing", s));
    }
}
