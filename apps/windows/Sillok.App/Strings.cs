using System.Globalization;
using Microsoft.Windows.ApplicationModel.Resources;

namespace Sillok.App;

/// <summary>The app's text from Strings/&lt;language&gt;/Resources.resw, in the user's language.</summary>
internal static class Strings
{
    private static readonly ResourceLoader Loader = new();

    public static string Get(string name) => Loader.GetString(name);

    public static string Format(string name, params object?[] args) =>
        string.Format(CultureInfo.CurrentCulture, Get(name), args);

    /// <summary>"1:02:03" or "2:03".</summary>
    public static string Time(long ms)
    {
        var t = TimeSpan.FromMilliseconds(Math.Max(ms, 0));
        return t.TotalHours >= 1
            ? string.Create(CultureInfo.InvariantCulture, $"{(int)t.TotalHours}:{t.Minutes:00}:{t.Seconds:00}")
            : string.Create(CultureInfo.InvariantCulture, $"{t.Minutes}:{t.Seconds:00}");
    }

    public static string Bytes(long bytes) => bytes switch
    {
        >= 1L << 30 => (bytes / (double)(1L << 30)).ToString("0.0 GB", CultureInfo.CurrentCulture),
        >= 1L << 20 => (bytes / (double)(1L << 20)).ToString("0 MB", CultureInfo.CurrentCulture),
        _ => (bytes / 1024.0).ToString("0 KB", CultureInfo.CurrentCulture),
    };

    public static string Step(RunStep step) => Get("Step" + step);

    public static string Date(long ms) =>
        DateTimeOffset.FromUnixTimeMilliseconds(ms).LocalDateTime.ToString("g", CultureInfo.CurrentCulture);
}
