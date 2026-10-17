using System.Net;
using System.Net.Http.Headers;

namespace Sillok.Core;

public sealed class InsufficientDiskSpaceException(long needed) : Exception($"저장 공간이 부족합니다. {needed / 1_048_576}MB가 더 필요합니다");

public sealed class HttpModelDownloader(ModelStore store, HttpClient client)
{
    public async Task DownloadAsync(ModelEntry entry, IProgress<long>? progress = null, CancellationToken cancellation = default)
    {
        Directory.CreateDirectory(store.DownloadsDirectory);
        var part = Path.Combine(store.DownloadsDirectory, entry.FileName + ".part");
        Exception? failure = null;
        foreach (var url in entry.Urls)
        {
            try
            {
                await DownloadFromAsync(url, part, entry, progress, cancellation);
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
        throw failure ?? new InvalidOperationException($"{entry.FileName}의 다운로드 주소가 없습니다");
    }

    private async Task DownloadFromAsync(string url, string part, ModelEntry entry, IProgress<long>? progress, CancellationToken cancellation)
    {
        var existing = File.Exists(part) ? new FileInfo(part).Length : 0;
        if (existing > entry.Size) existing = 0;
        if (store.AvailableDiskSpace() < entry.Size - existing) throw new InsufficientDiskSpaceException(entry.Size - existing);

        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        if (existing > 0) request.Headers.Range = new RangeHeaderValue(existing, null);
        using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellation);
        response.EnsureSuccessStatusCode();
        if (response.StatusCode != HttpStatusCode.PartialContent) existing = 0;

        await using var input = await response.Content.ReadAsStreamAsync(cancellation);
        await using var output = new FileStream(part, existing > 0 ? FileMode.Append : FileMode.Create, FileAccess.Write, FileShare.None);
        var buffer = new byte[1 << 20];
        var received = existing;
        progress?.Report(received);
        int read;
        while ((read = await input.ReadAsync(buffer, cancellation)) > 0)
        {
            await output.WriteAsync(buffer.AsMemory(0, read), cancellation);
            received += read;
            progress?.Report(received);
        }
    }
}
