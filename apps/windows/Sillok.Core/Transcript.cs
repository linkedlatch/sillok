using System.Text.Json.Nodes;

namespace Sillok;

/// <summary>
/// The transcript document of sillok_job_run(), read for display. The app
/// keeps the document itself as the source of truth and changes only speaker
/// names in it, so fields it does not show (words, models) survive.
/// </summary>
public sealed class Transcript
{
    public sealed record Speaker(int Id, string? Name);
    public sealed record Segment(long StartMs, long EndMs, string Text, int? Speaker, string Language = "");
    /// <summary>A model the run used: its device as the library names it, and whether a GPU request fell back to the CPU.</summary>
    public sealed record ModelUse(string Name, string Device, bool FellBackToCpu);

    public string Json { get; }
    public long DurationMs { get; }
    public IReadOnlyList<Speaker> Speakers { get; }
    public IReadOnlyList<Segment> Segments { get; }
    /// <summary>By role: "transcription", "voice_detection", "diarization".</summary>
    public IReadOnlyDictionary<string, ModelUse> Models { get; }

    public Transcript(string json)
    {
        Json = json;
        var root = JsonNode.Parse(json)!.AsObject();
        DurationMs = (long)root["duration_ms"]!;
        Speakers = root["speakers"]!.AsArray()
            .Select(s => new Speaker((int)s!["id"]!, (string?)s["name"]))
            .ToList();
        Segments = ParseSegments(root["segments"]!.AsArray());
        Models = (root["models"]?.AsObject() ?? [])
            .ToDictionary(m => m.Key, m => new ModelUse(
                (string?)m.Value!["name"] ?? "", (string?)m.Value["device"] ?? "", (bool?)m.Value["fell_back_to_cpu"] ?? false));
    }

    /// <summary>Segments of a JSON array as in the document.</summary>
    public static IReadOnlyList<Segment> ParseSegments(JsonArray segments) =>
        segments.Select(s => new Segment((long)s!["start_ms"]!, (long)s["end_ms"]!, (string)s["text"]!, (int?)s["speaker"],
            (string?)s["language"] ?? "")).ToList();

    public string? SpeakerName(int id) => Speakers.FirstOrDefault(s => s.Id == id)?.Name;

    /// <summary>A copy with speakers renamed; a blank name clears it.</summary>
    public Transcript WithSpeakerNames(IReadOnlyDictionary<int, string> names)
    {
        var root = JsonNode.Parse(Json)!.AsObject();
        foreach (var speaker in root["speakers"]!.AsArray())
        {
            if (names.TryGetValue((int)speaker!["id"]!, out var name))
            {
                speaker["name"] = string.IsNullOrWhiteSpace(name) ? null : name.Trim();
            }
        }
        return new Transcript(root.ToJsonString());
    }

    /// <summary>Index of the segment playing at <paramref name="positionMs"/>, or -1.</summary>
    public int SegmentAt(long positionMs)
    {
        int low = 0, high = Segments.Count - 1, found = -1;
        while (low <= high)
        {
            var mid = (low + high) / 2;
            if (Segments[mid].StartMs <= positionMs)
            {
                found = mid;
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }
        return found >= 0 && positionMs < Segments[found].EndMs ? found : -1;
    }
}
