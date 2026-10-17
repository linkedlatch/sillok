using System.Runtime.InteropServices;
using Windows.Media.Core;
using Windows.Media.Playback;

namespace Sillok.Windows.Services;

public static partial class PowerRequest
{
    private const uint Continuous = 0x80000000;
    private const uint SystemRequired = 0x00000001;

    public static IDisposable KeepAwake()
    {
        SetThreadExecutionState(Continuous | SystemRequired);
        return new Release();
    }

    [LibraryImport("kernel32.dll")]
    private static partial uint SetThreadExecutionState(uint flags);

    private sealed class Release : IDisposable
    {
        public void Dispose() => SetThreadExecutionState(Continuous);
    }
}

public sealed class Playback
{
    private readonly MediaPlayer player = new();
    private string? currentPath;
    private TimeSpan? pendingPosition;

    public Playback()
    {
        player.MediaOpened += (_, _) =>
        {
            if (pendingPosition is { } position) player.PlaybackSession.Position = position;
            pendingPosition = null;
            player.Play();
        };
        player.PlaybackSession.PlaybackStateChanged += (_, _) => StateChanged?.Invoke();
    }

    public event Action? StateChanged;

    public bool IsPlaying => player.PlaybackSession.PlaybackState == MediaPlaybackState.Playing;

    public string? CurrentPath => currentPath;

    public void Play(string path, double seconds)
    {
        var position = TimeSpan.FromSeconds(seconds);
        if (!string.Equals(currentPath, path, StringComparison.OrdinalIgnoreCase))
        {
            currentPath = path;
            pendingPosition = position;
            player.Source = MediaSource.CreateFromUri(new Uri(path));
            return;
        }
        player.PlaybackSession.Position = position;
        player.Play();
    }

    public void Pause() => player.Pause();

    public void Stop()
    {
        player.Pause();
        player.Source = null;
        currentPath = null;
    }
}
