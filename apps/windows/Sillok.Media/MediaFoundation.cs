using System.Runtime.InteropServices;
using Windows.Win32;

namespace Sillok.Media;

/// <summary>The file has no audio the platform can decode.</summary>
public sealed class UnsupportedMediaException(string message, Exception? inner = null) : IOException(message, inner);

internal static class MediaFoundation
{
    private static readonly Lazy<bool> Started = new(() =>
    {
        // Started once for the process and never shut down: Media Foundation
        // stays usable for every file the app opens.
        PInvoke.MFStartup(PInvoke.MF_VERSION, 0).ThrowOnFailure();
        return true;
    });

    public static void Start() => _ = Started.Value;

    /// <summary>Releases a COM object now instead of at garbage collection; Media Foundation recycles its samples.</summary>
    public static void Release(object? com)
    {
        if (com is not null && Marshal.IsComObject(com)) Marshal.ReleaseComObject(com);
    }
}
