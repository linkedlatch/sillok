namespace Sillok.Tests;

public class JobStoreTests
{
    [Fact]
    public void ListsOnlyCompletedJobs()
    {
        var store = new JobStore(Directory.CreateTempSubdirectory("sillok-jobs").FullName);
        var unfinished = store.Create();
        var done = store.Create();
        var stats = new JobStats(new Dictionary<string, long> { ["read"] = 12, ["transcribe"] = 34 }, 8, "mf");
        var entry = store.Complete(done, "{}", "lecture.m4a", 60_000, stats);
        Assert.Equal("lecture", entry.Title);
        var listed = Assert.Single(store.List());
        Assert.Equal(entry.Id, listed.Id);
        Assert.Equal(34, listed.Stats!.TimingsMs["transcribe"]);
        Assert.Equal("mf", listed.Stats.Decoder);

        store.RemoveIncomplete();
        Assert.False(Directory.Exists(unfinished));
        store.Rename(entry, "Week 1");
        Assert.Equal("Week 1", store.Get(entry.Id)?.Title);
        store.Delete(entry.Id);
        Assert.Empty(store.List());
    }
}
