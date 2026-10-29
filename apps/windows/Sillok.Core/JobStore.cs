using System.Globalization;
using System.Text.Json.Nodes;

namespace Sillok;

/// <summary>How a job ran: wall time per step ("read", "analyze", ...), the CPU threads and the audio decoder.</summary>
public sealed record JobStats(IReadOnlyDictionary<string, long> TimingsMs, int Threads, string? Decoder);

/// <summary>A finished transcription in the history.</summary>
public sealed record JobEntry(string Id, string Title, string Source, long CreatedMs, long DurationMs, JobStats? Stats);

/// <summary>
/// The history, one folder per job under a root folder (docs/GUI-DESIGN.md
/// G7), laid out as in the Android app: transcript.json (the library's
/// document, with speaker names), audio.m4a (the listening copy) and
/// meta.json. A folder counts as a job once its meta.json exists, which is
/// written last.
/// </summary>
public sealed class JobStore(string root)
{
    public const string TranscriptFile = "transcript.json";
    public const string AudioFile = "audio.m4a";
    private const string MetaFile = "meta.json";

    public IReadOnlyList<JobEntry> List() =>
        (Directory.Exists(root) ? Directory.GetDirectories(root) : [])
            .Select(TryRead)
            .OfType<JobEntry>()
            .OrderByDescending(e => e.CreatedMs)
            .ToList();

    /// <summary>A new, empty folder for a job that is about to run.</summary>
    public string Create()
    {
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        var dir = Path.Combine(root, stamp);
        for (var n = 1; Directory.Exists(dir); n++) dir = Path.Combine(root, $"{stamp}-{n}");
        Directory.CreateDirectory(dir);
        return dir;
    }

    public JobEntry Complete(string dir, string transcript, string source, long durationMs, JobStats? stats = null)
    {
        WriteAtomically(Path.Combine(dir, TranscriptFile), transcript);
        var title = Path.GetFileNameWithoutExtension(source);
        var entry = new JobEntry(Path.GetFileName(dir), title.Length > 0 ? title : source, source,
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), durationMs, stats);
        Write(entry);
        return entry;
    }

    public JobEntry? Get(string id) => TryRead(Path.Combine(root, id));

    public void Rename(JobEntry entry, string title) => Write(entry with { Title = title });

    public void Delete(string id)
    {
        var dir = Path.Combine(root, id);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
    }

    /// <summary>Removes folders of jobs that never finished; call when no job is running.</summary>
    public void RemoveIncomplete()
    {
        if (!Directory.Exists(root)) return;
        foreach (var dir in Directory.GetDirectories(root))
        {
            if (!File.Exists(Path.Combine(dir, MetaFile))) Directory.Delete(dir, recursive: true);
        }
    }

    public string TranscriptPath(string id) => Path.Combine(root, id, TranscriptFile);
    public string AudioPath(string id) => Path.Combine(root, id, AudioFile);

    public void SaveTranscript(string id, string transcript) => WriteAtomically(TranscriptPath(id), transcript);

    private static JobEntry? TryRead(string dir)
    {
        try
        {
            var json = JsonNode.Parse(File.ReadAllText(Path.Combine(dir, MetaFile)))!.AsObject();
            JobStats? stats = null;
            if (json["stats"] is JsonObject s)
            {
                stats = new JobStats(
                    s["timings_ms"]!.AsObject().ToDictionary(t => t.Key, t => (long)t.Value!),
                    (int)s["threads"]!, (string?)s["decoder"]);
            }
            return new JobEntry(Path.GetFileName(dir), (string)json["title"]!, (string)json["source"]!,
                (long)json["created_ms"]!, (long)json["duration_ms"]!, stats);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Text.Json.JsonException
                                      or NullReferenceException or InvalidOperationException or FormatException)
        {
            return null;
        }
    }

    private void Write(JobEntry entry)
    {
        var json = new JsonObject
        {
            ["title"] = entry.Title,
            ["source"] = entry.Source,
            ["created_ms"] = entry.CreatedMs,
            ["duration_ms"] = entry.DurationMs,
        };
        if (entry.Stats is { } stats)
        {
            var timings = new JsonObject();
            foreach (var (key, value) in stats.TimingsMs) timings[key] = value;
            json["stats"] = new JsonObject { ["timings_ms"] = timings, ["threads"] = stats.Threads, ["decoder"] = stats.Decoder };
        }
        WriteAtomically(Path.Combine(root, entry.Id, MetaFile), json.ToJsonString(new() { WriteIndented = true }));
    }

    private static void WriteAtomically(string path, string text)
    {
        var temp = path + ".tmp";
        File.WriteAllText(temp, text);
        File.Move(temp, path, overwrite: true);
    }
}
