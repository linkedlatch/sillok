using System.Text.Json;
using System.Text.Json.Serialization;

namespace Sillok.Core;

public interface IVersionedDocument<TSelf> where TSelf : IVersionedDocument<TSelf>
{
    static abstract int CurrentSchemaVersion { get; }

    int SchemaVersion { get; }
}

public sealed class UnsupportedSchemaVersionException(int version) : Exception($"지원하지 않는 저장 형식 버전입니다: {version}");

public static class VersionedStore
{
    public static T Load<T>(string path) where T : IVersionedDocument<T>
    {
        var json = File.ReadAllText(path);
        using (var document = JsonDocument.Parse(json))
        {
            var version = document.RootElement.GetProperty("schemaVersion").GetInt32();
            if (version > T.CurrentSchemaVersion) throw new UnsupportedSchemaVersionException(version);
        }
        return SillokJson.Deserialize<T>(json);
    }

    public static void Save<T>(T document, string path) where T : IVersionedDocument<T>
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = $"{path}.{Guid.NewGuid():N}.tmp";
        File.WriteAllText(temporary, SillokJson.Serialize(document));
        File.Move(temporary, path, overwrite: true);
    }
}

public sealed record Checkpoint : IVersionedDocument<Checkpoint>
{
    public static int CurrentSchemaVersion => 1;

    public int SchemaVersion { get; init; } = CurrentSchemaVersion;
    public required string ModelId { get; init; }
    public required TranscriptionOptions Options { get; init; }
    public required IReadOnlyList<AudioChunk> Chunks { get; init; }
    public TranscriptionState State { get; init; } = new();

    [JsonIgnore]
    public double ResumeFrom => State.NextChunk == 0 ? 0 : Chunks[State.NextChunk - 1].End;
}
