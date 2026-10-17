using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using Sillok.Core;

namespace Sillok.Windows.Services;

public sealed class ModelDownloader(ModelStore store)
{
    private static readonly HttpClient Client = new() { Timeout = Timeout.InfiniteTimeSpan };

    public async Task DownloadAsync(ModelEntry entry, IProgress<long> progress, CancellationToken cancellation)
    {
        Directory.CreateDirectory(store.DownloadsDirectory);
        var part = Path.Combine(store.DownloadsDirectory, entry.FileName + ".part");
        Exception? failure = null;
        foreach (var url in entry.Urls)
        {
            try
            {
                await DownloadFromAsync(url, part, entry, progress, cancellation).ConfigureAwait(false);
                store.InstallVerified(part, entry);
                return;
            }
            catch (HttpRequestException error)
            {
                failure = error;
            }
            catch (IOException error)
            {
                failure = error;
            }
        }
        throw failure ?? new InvalidOperationException($"no download source for {entry.FileName}");
    }

    private async Task DownloadFromAsync(string url, string part, ModelEntry entry, IProgress<long> progress, CancellationToken cancellation)
    {
        var existing = File.Exists(part) ? new FileInfo(part).Length : 0;
        if (existing > entry.Size) existing = 0;
        if (existing == entry.Size) return;
        if (store.AvailableDiskSpace() < entry.Size - existing) throw new InsufficientDiskSpaceException(entry.Size - existing);

        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        if (existing > 0) request.Headers.Range = new RangeHeaderValue(existing, null);
        using var response = await Client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellation).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        if (response.StatusCode != HttpStatusCode.PartialContent) existing = 0;

        await using var input = await response.Content.ReadAsStreamAsync(cancellation).ConfigureAwait(false);
        await using var output = new FileStream(part, existing > 0 ? FileMode.Append : FileMode.Create, FileAccess.Write, FileShare.None);
        var buffer = new byte[1 << 20];
        var received = existing;
        progress.Report(received);
        var sinceReport = Stopwatch.StartNew();
        int read;
        while ((read = await input.ReadAsync(buffer, cancellation).ConfigureAwait(false)) > 0)
        {
            await output.WriteAsync(buffer.AsMemory(0, read), cancellation).ConfigureAwait(false);
            received += read;
            if (sinceReport.ElapsedMilliseconds >= 250)
            {
                progress.Report(received);
                sinceReport.Restart();
            }
        }
        progress.Report(received);
    }
}
