using System.Net;
using System.Net.Http.Headers;

namespace Sillok;

/// <summary>Some files failed their hash check and were removed.</summary>
public sealed class ModelIntegrityException(IReadOnlyList<string> files) : IOException($"damaged: {string.Join(", ", files)}")
{
    public IReadOnlyList<string> Files { get; } = files;
}

/// <summary>
/// Downloads the model catalog into a folder (docs/DESIGN.md D9). Each file
/// downloads to "&lt;name&gt;.part" and resumes from there; hashes are checked
/// once every file is complete, and damaged files are removed so the next
/// attempt downloads them again.
/// </summary>
public sealed class ModelDownloader(string dir, HttpClient http)
{
    /// <summary>Catalog files that are not complete in the folder. Archives (Core ML, Apple only) never are here.</summary>
    public IReadOnlyList<ModelFile> Missing()
    {
        var status = SillokLibrary.Check(dir, verifyHash: false);
        return SillokLibrary.Catalog().Where(m => status.GetValueOrDefault(m.File) != "ok").ToList();
    }

    /// <summary>Bytes already downloaded of <paramref name="files"/>, counting partial downloads.</summary>
    public long Downloaded(IEnumerable<ModelFile> files) =>
        files.Sum(m => Math.Min(new FileInfo(PartPath(m)).Exists ? new FileInfo(PartPath(m)).Length : 0, m.Size));

    /// <param name="progress">Receives the bytes downloaded so far of all <paramref name="files"/>.</param>
    public async Task DownloadAsync(IReadOnlyList<ModelFile> files, IProgress<long>? progress, CancellationToken cancel)
    {
        Directory.CreateDirectory(dir);
        var done = Downloaded(files);
        foreach (var model in files)
        {
            await DownloadFileAsync(model, bytes =>
            {
                done += bytes;
                progress?.Report(done);
            }, cancel).ConfigureAwait(false);
        }
        Verify();
    }

    /// <summary>Checks every file's hash; removes and reports damaged ones.</summary>
    public void Verify()
    {
        var damaged = SillokLibrary.Check(dir, verifyHash: true).Where(s => s.Value != "ok").Select(s => s.Key).ToList();
        foreach (var file in damaged) File.Delete(Path.Combine(dir, file));
        if (damaged.Count > 0) throw new ModelIntegrityException(damaged);
    }

    private string PartPath(ModelFile model) => Path.Combine(dir, model.File + ".part");

    private async Task DownloadFileAsync(ModelFile model, Action<long> onBytes, CancellationToken cancel)
    {
        var target = Path.Combine(dir, model.File);
        var part = PartPath(model);
        File.Delete(target);
        var start = File.Exists(part) ? new FileInfo(part).Length : 0;
        if (start > model.Size)
        {
            File.Delete(part);
            start = 0;
        }
        if (start < model.Size)
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, model.Url);
            if (start > 0) request.Headers.Range = new RangeHeaderValue(start, null);
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancel).ConfigureAwait(false);
            var append = response.StatusCode switch
            {
                HttpStatusCode.PartialContent => true,
                HttpStatusCode.OK => false,
                _ => throw new IOException($"HTTP {(int)response.StatusCode} for {model.File}"),
            };
            if (!append && start > 0) onBytes(-start);  // the server restarts from the beginning
            await using var input = await response.Content.ReadAsStreamAsync(cancel).ConfigureAwait(false);
            await using var output = new FileStream(part, append ? FileMode.Append : FileMode.Create, FileAccess.Write);
            var buffer = new byte[1 << 16];
            int read;
            while ((read = await input.ReadAsync(buffer, cancel).ConfigureAwait(false)) > 0)
            {
                await output.WriteAsync(buffer.AsMemory(0, read), cancel).ConfigureAwait(false);
                onBytes(read);
            }
        }
        if (new FileInfo(part).Length != model.Size) throw new IOException($"incomplete download of {model.File}");
        File.Move(part, target);
    }
}
