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

    /// <summary>"large-v3 turbo / q5_0": the model, then its quantization.</summary>
    public static string ModelName(CatalogModel model) =>
        model.Family.Replace("-turbo", " turbo", StringComparison.Ordinal) + " / " + model.Quantization;

    /// <summary>
    /// "Accuracy high · speed medium · compressed · 574 MB": one scale for all
    /// models with turbo q5_0 (recommended) in the middle. Without compression a
    /// model was 1.7 to 2 times slower on a CPU and a GTX 1060, a step down.
    /// </summary>
    public static string ModelSummary(CatalogModel model)
    {
        string[] speeds = ["SpeedVerySlow", "SpeedSlow", "SpeedMedium", "SpeedFast", "SpeedVeryFast", "SpeedFastest"];
        var (accuracy, speed) = model.Family switch
        {
            "large-v3" => ("AccuracyHighest", 1),
            "large-v3-turbo" => ("AccuracyHigh", 2),
            "small" => ("AccuracyMedium", 3),
            "base" => ("AccuracyLow", 4),
            _ => ("AccuracyVeryLow", 5),
        };
        if (model.Quantization == "f16") speed--;
        var compression = model.Quantization switch
        {
            "f16" => "QuantF16",
            "q8_0" => "QuantQ8",
            _ => "QuantQ5",
        };
        return Format("ModelSummary", Get(accuracy), Get(speeds[speed]), Get(compression), Bytes(model.Size));
    }

    /// <summary>"Automatic · speakers told apart · large-v3 turbo / q5_0"</summary>
    public static string SettingsSummary(JobSettings settings, CatalogModel model) =>
        string.Join(" · ", Get("Language_" + settings.Language), Get(settings.Diarize ? "DiarizeOn" : "DiarizeOff"), ModelName(model));
}
