using Sillok.Core;
using Windows.Media.MediaProperties;
using Windows.Media.Transcoding;
using Windows.Storage;

namespace Sillok.Windows.Services;

public static class AudioConverter
{
    public static async Task ConvertToWavAsync(string sourcePath, string targetPath, CancellationToken cancellation)
    {
        var directory = Path.GetDirectoryName(targetPath)!;
        Directory.CreateDirectory(directory);
        var partName = Path.GetFileName(targetPath) + ".part";
        var source = await StorageFile.GetFileFromPathAsync(sourcePath);
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var target = await folder.CreateFileAsync(partName, CreationCollisionOption.ReplaceExisting);

        var profile = MediaEncodingProfile.CreateWav(AudioEncodingQuality.Low);
        profile.Audio = AudioEncodingProperties.CreatePcm(PcmFormat.SampleRate, 1, 16);
        var prepared = await new MediaTranscoder().PrepareFileTranscodeAsync(source, target, profile);
        if (!prepared.CanTranscode) throw new InvalidDataException($"cannot transcode this file ({prepared.FailureReason})");
        await prepared.TranscodeAsync().AsTask(cancellation);
        File.Move(Path.Combine(directory, partName), targetPath, overwrite: true);
    }
}
