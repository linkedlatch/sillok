using Sillok.Core;
using Windows.Networking.BackgroundTransfer;
using Windows.Storage;

namespace Sillok.Windows.Services;

public sealed class ModelDownloader(ModelStore store)
{
    public async Task DownloadAsync(ModelEntry entry, IProgress<long> progress, CancellationToken cancellation)
    {
        if (store.AvailableDiskSpace() < entry.Size) throw new InsufficientDiskSpaceException(entry.Size);
        Directory.CreateDirectory(store.DownloadsDirectory);
        var folder = await StorageFolder.GetFolderFromPathAsync(store.DownloadsDirectory);
        var partPath = Path.Combine(store.DownloadsDirectory, entry.FileName + ".part");
        var report = new Progress<DownloadOperation>(operation => progress.Report((long)operation.Progress.BytesReceived));

        var existing = (await BackgroundDownloader.GetCurrentDownloadsAsync())
            .FirstOrDefault(operation => string.Equals(operation.ResultFile?.Path, partPath, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            await existing.AttachAsync().AsTask(cancellation, report);
        }
        else
        {
            Exception? failure = null;
            foreach (var url in entry.Urls)
            {
                try
                {
                    var file = await folder.CreateFileAsync(Path.GetFileName(partPath), CreationCollisionOption.ReplaceExisting);
                    var operation = new BackgroundDownloader().CreateDownload(new Uri(url), file);
                    await operation.StartAsync().AsTask(cancellation, report);
                    failure = null;
                    break;
                }
                catch (Exception error) when (error is not OperationCanceledException)
                {
                    failure = error;
                }
            }
            if (failure is not null) throw failure;
        }
        store.InstallVerified(partPath, entry);
    }
}
