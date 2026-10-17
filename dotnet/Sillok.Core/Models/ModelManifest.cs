using System.Security.Cryptography;

namespace Sillok.Core;

public enum ModelKind
{
    Whisper,
    CoreMLEncoder,
    Vad,
    Diarization,
}

public sealed record ModelEntry
{
    public required string Id { get; init; }
    public required ModelKind Kind { get; init; }
    public required string DisplayName { get; init; }
    public required string FileName { get; init; }
    public required long Size { get; init; }
    public required string Sha256 { get; init; }
    public required IReadOnlyList<string> Urls { get; init; }
    public string? CoreMLEncoderId { get; init; }
    public required string License { get; init; }
}

public sealed record ModelManifest(int Version, IReadOnlyList<ModelEntry> Models)
{
    public static ModelManifest Load(string path) => SillokJson.Deserialize<ModelManifest>(File.ReadAllText(path));

    public ModelEntry? Entry(string id) => Models.FirstOrDefault(entry => entry.Id == id);
}

public static class ModelSelection
{
    public const string DefaultWhisperId = "large-v3-turbo-q5_0";
    public const string VadId = "silero-v6.2.0";
    public const string DiarizationId = "nemotron-3-diarization-q8_0";

    public static IReadOnlyList<ModelEntry> RequiredEntries(string whisperId, bool diarization, ModelManifest manifest)
    {
        if (manifest.Entry(whisperId) is not { } whisper) return [];
        var entries = new List<ModelEntry> { whisper };
        if (manifest.Entry(VadId) is { } vad) entries.Add(vad);
        if (diarization && manifest.Entry(DiarizationId) is { } diarizer) entries.Add(diarizer);
        return entries;
    }
}

public sealed class ModelChecksumException(string fileName) : Exception($"{fileName} failed checksum verification")
{
    public string FileName { get; } = fileName;
}

public sealed class InsufficientDiskSpaceException(long needed) : Exception($"not enough disk space ({needed} bytes required)")
{
    public long Needed { get; } = needed;
}

public sealed class ModelStore(string root, ModelManifest manifest)
{
    public ModelManifest Manifest => manifest;

    public string DownloadsDirectory => Path.Combine(root, ".downloads");

    public string Location(ModelEntry entry) => Path.Combine(root, entry.FileName);

    public bool IsInstalled(ModelEntry entry)
    {
        var file = new FileInfo(Location(entry));
        return file.Exists && file.Length == entry.Size;
    }

    public void InstallVerified(string downloadedFile, ModelEntry entry)
    {
        string hash;
        using (var stream = File.OpenRead(downloadedFile))
        {
            hash = Convert.ToHexStringLower(SHA256.HashData(stream));
        }
        if (!string.Equals(hash, entry.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            File.Delete(downloadedFile);
            throw new ModelChecksumException(entry.FileName);
        }
        Directory.CreateDirectory(root);
        File.Move(downloadedFile, Location(entry), overwrite: true);
    }

    public void Remove(ModelEntry entry)
    {
        if (File.Exists(Location(entry))) File.Delete(Location(entry));
    }

    public long AvailableDiskSpace()
    {
        Directory.CreateDirectory(root);
        return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(root))!).AvailableFreeSpace;
    }
}
