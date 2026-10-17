using System.Diagnostics;

namespace Sillok.Core;

public sealed class FfmpegNotFoundException() : Exception("WAV가 아닌 파일을 읽으려면 ffmpeg가 필요합니다. ffmpeg를 설치하거나 16kHz 모노 WAV로 변환해 주세요");

public static class FfmpegConverter
{
    public static async Task ConvertAsync(string input, string output, CancellationToken cancellation = default)
    {
        var start = new ProcessStartInfo("ffmpeg")
        {
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        foreach (var argument in new[] { "-nostdin", "-v", "error", "-y", "-i", input, "-ac", "1", "-ar", PcmFormat.SampleRate.ToString(), "-c:a", "pcm_s16le", "-f", "wav", output })
        {
            start.ArgumentList.Add(argument);
        }
        Process process;
        try
        {
            process = Process.Start(start) ?? throw new FfmpegNotFoundException();
        }
        catch (System.ComponentModel.Win32Exception)
        {
            throw new FfmpegNotFoundException();
        }
        using (process)
        {
            var errors = process.StandardError.ReadToEndAsync(cancellation);
            try
            {
                await process.WaitForExitAsync(cancellation);
            }
            catch (OperationCanceledException)
            {
                process.Kill();
                throw;
            }
            if (process.ExitCode != 0) throw new InvalidDataException($"오디오를 변환하지 못했습니다: {(await errors).Trim()}");
        }
    }
}
