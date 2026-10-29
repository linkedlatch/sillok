using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.Storage.Pickers;
using Sillok.Media;

namespace Sillok.App;

public enum ModelPhase { Checking, Missing, Downloading, Verifying, Ready }

public enum Screen { Home, Setup, Running, Result }

/// <summary>Progress of the running transcription: the current step, its fraction and when it began.</summary>
public sealed record RunInfo(string Name, IReadOnlyList<RunStep> Steps, RunStep Step, double Fraction, long StepStartedMs, bool Cancelling = false);

/// <summary>
/// The app's state and actions, shared by every page; pages listen to
/// <see cref="ObservableObject.PropertyChanged"/>. Work runs off the UI
/// thread and reports back through the window's dispatcher.
/// </summary>
public sealed partial class AppState : ObservableObject, IDisposable
{
    public static string DataDir { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Sillok");

    private readonly string _modelsDir = Path.Combine(DataDir, "models");
    private readonly string _settingsPath = Path.Combine(DataDir, "settings.json");
    private readonly HttpClient _http = new();
    private readonly ModelDownloader _downloader;
    private DispatcherQueue? _ui;
    private Window? _window;
    private CancellationTokenSource? _download;
    private TranscriptionRunner? _runner;
    private readonly Stopwatch _clock = Stopwatch.StartNew();

    public AppState()
    {
        Store = new JobStore(Path.Combine(DataDir, "jobs"));
        _downloader = new ModelDownloader(_modelsDir, _http);
        Settings = LoadSettings();
    }

    public JobStore Store { get; }
    public ObservableCollection<JobEntry> History { get; } = [];
    public IReadOnlyList<ComputeDevice> Gpus { get; private set; } = [];
    public static string Version => $"{typeof(AppState).Assembly.GetName().Version}\n{SillokLibrary.Version}";

    [ObservableProperty] public partial ModelPhase ModelPhase { get; set; }
    [ObservableProperty] public partial long ModelTotal { get; set; }
    [ObservableProperty] public partial long ModelDone { get; set; }
    [ObservableProperty] public partial long ModelSpeed { get; set; }
    [ObservableProperty] public partial string? ModelError { get; set; }
    [ObservableProperty] public partial Screen Screen { get; set; }
    [ObservableProperty] public partial RunInfo? Run { get; set; }
    /// <summary>A failure to show once; the window clears it.</summary>
    [ObservableProperty] public partial string? Error { get; set; }

    public JobSettings Settings { get; private set; }
    /// <summary>The file the setup page is about to transcribe.</summary>
    public (string Path, string Name)? PendingFile { get; private set; }
    /// <summary>The job the result page shows.</summary>
    public string? ResultId { get; private set; }

    public void Dispose()
    {
        _download?.Dispose();
        _http.Dispose();
        GC.SuppressFinalize(this);
    }

    public void Attach(Window window)
    {
        _window = window;
        _ui = window.DispatcherQueue;
        Directory.CreateDirectory(DataDir);
        Store.RemoveIncomplete();
        Gpus = SillokLibrary.Devices().Where(d => d.IsGpu).ToList();
        // A GPU when there is one (docs/DESIGN.md D15: the app chooses).
        if (Settings.GpuIndex is null && Gpus.Count > 0 && !File.Exists(_settingsPath)) Settings = Settings with { GpuIndex = 0 };
        if (Settings.GpuIndex >= Gpus.Count) Settings = Settings with { GpuIndex = null };
        RefreshHistory();
        _ = CheckModelsAsync();
    }

    private void OnUi(Action action) => _ui?.TryEnqueue(() => action());

    // --- Models --------------------------------------------------------------

    private async Task CheckModelsAsync()
    {
        ModelPhase = ModelPhase.Checking;
        var missing = await Task.Run(() => _downloader.Missing());
        ModelTotal = missing.Sum(m => m.Size);
        ModelDone = _downloader.Downloaded(missing);
        ModelPhase = missing.Count == 0 ? ModelPhase.Ready : ModelPhase.Missing;
    }

    public async Task StartDownloadAsync()
    {
        if (_download is not null) return;
        _download = new CancellationTokenSource();
        ModelError = null;
        var missing = await Task.Run(() => _downloader.Missing());
        ModelTotal = missing.Sum(m => m.Size);
        ModelPhase = ModelPhase.Downloading;
        long last = _downloader.Downloaded(missing);
        var lastAt = _clock.ElapsedMilliseconds;
        var progress = new Progress<long>(done =>
        {
            // Progress<T> posts to the UI thread it was created on.
            var now = _clock.ElapsedMilliseconds;
            if (now - lastAt >= 500)
            {
                ModelSpeed = (done - last) * 1000 / (now - lastAt);
                last = done;
                lastAt = now;
            }
            ModelDone = done;
            if (done >= ModelTotal) ModelPhase = ModelPhase.Verifying;
        });
        try
        {
            var token = _download.Token;
            await Task.Run(() => _downloader.DownloadAsync(missing, progress, token), token);
            ModelPhase = ModelPhase.Ready;
        }
        catch (OperationCanceledException)
        {
            await CheckModelsAsync();
        }
        catch (ModelIntegrityException)
        {
            await CheckModelsAsync();
            ModelError = Strings.Get("ModelsDamaged");
        }
        catch (Exception e) when (e is IOException or HttpRequestException)
        {
            await CheckModelsAsync();
            ModelError = Strings.Format("ModelsDownloadFailed", e.Message);
        }
        finally
        {
            _download.Dispose();
            _download = null;
        }
    }

    public void StopDownload() => _download?.Cancel();

    /// <summary>Re-reads every model file and compares its hash with the catalog.</summary>
    public async Task VerifyModelsAsync()
    {
        if (Run is not null || _download is not null) return;
        ModelPhase = ModelPhase.Verifying;
        try
        {
            await Task.Run(_downloader.Verify);
            ModelPhase = ModelPhase.Ready;
            Error = Strings.Get("ModelsVerified");
        }
        catch (ModelIntegrityException)
        {
            await CheckModelsAsync();
            Error = Strings.Get("ModelsDamaged");
        }
    }

    // --- Navigation ------------------------------------------------------------

    public async Task OpenFileAsync()
    {
        if (_window is null) return;
        var picker = new FileOpenPicker(_window.AppWindow.Id)
        {
            SuggestedStartLocation = PickerLocationId.MusicLibrary,
        };
        foreach (var type in MediaFileTypes) picker.FileTypeFilter.Add(type);
        var file = await picker.PickSingleFileAsync();
        if (file is null) return;
        PendingFile = (file.Path, Path.GetFileName(file.Path));
        Screen = Screen.Setup;
    }

    // Types Media Foundation decodes on a standard Windows; "*" lets users try others.
    private static readonly string[] MediaFileTypes =
        [".m4a", ".mp3", ".wav", ".aac", ".wma", ".flac", ".mp4", ".mov", ".m4v", ".wmv", ".3gp", "*"];

    public void ShowResult(string id)
    {
        ResultId = id;
        Screen = Screen.Result;
    }

    public void ShowRunning()
    {
        if (Run is not null) Screen = Screen.Running;
    }

    /// <summary>Back to the list; a running job goes on.</summary>
    public void GoHome() => Screen = Screen.Home;

    // --- Transcription ----------------------------------------------------------

    public void Start(JobSettings settings)
    {
        if (Run is not null || PendingFile is not { } file) return;
        Settings = settings;
        SaveSettings();
        var steps = new List<RunStep> { RunStep.Read, RunStep.Analyze, RunStep.Transcribe };
        if (settings.Diarize) steps.Add(RunStep.Diarize);
        var runner = new TranscriptionRunner(Store, _modelsDir, Path.GetTempPath());
        _runner = runner;
        Run = new RunInfo(file.Name, steps, RunStep.Read, 0, _clock.ElapsedMilliseconds);
        Screen = Screen.Running;

        _ = Task.Run(() =>
        {
            var lastStep = RunStep.Read;
            var lastFraction = 0.0;
            JobEntry? entry = null;
            string? failure = null;
            try
            {
                entry = runner.Run(file.Path, file.Name, settings, (step, fraction) =>
                {
                    // Called often from the worker; pass on visible changes only.
                    if (step == lastStep && fraction - lastFraction < 0.001 && fraction < 1) return;
                    lastStep = step;
                    lastFraction = fraction;
                    OnUi(() => Progress(step, fraction));
                });
            }
            catch (Exception e)
            {
                failure = Describe(e);
            }
            OnUi(() => Finished(entry, failure));
        });
    }

    private void Progress(RunStep step, double fraction)
    {
        if (Run is not { } run) return;
        Run = step != run.Step
            ? run with { Step = step, Fraction = fraction, StepStartedMs = _clock.ElapsedMilliseconds }
            : run with { Fraction = fraction };
    }

    private void Finished(JobEntry? entry, string? failure)
    {
        _runner = null;
        Run = null;
        RefreshHistory();
        if (entry is not null)
        {
            ShowResult(entry.Id);
        }
        else
        {
            if (failure is not null) Error = failure;
            if (Screen == Screen.Running) Screen = Screen.Home;
        }
    }

    public void Cancel()
    {
        if (Run is not { } run) return;
        Run = run with { Cancelling = true };
        _runner?.Cancel();
    }

    public long Now => _clock.ElapsedMilliseconds;

    private static string Describe(Exception e) => e switch
    {
        UnsupportedMediaException => Strings.Format("ErrorUnsupportedFile", e.Message),
        SillokException { IsModelError: true } => Strings.Format("ErrorModel", e.Message),
        _ => Strings.Format("ErrorEngine", e.Message),
    };

    // --- History ------------------------------------------------------------------

    public void RefreshHistory()
    {
        History.Clear();
        foreach (var entry in Store.List()) History.Add(entry);
    }

    public JobEntry? Entry(string id) => History.FirstOrDefault(e => e.Id == id);

    public void Rename(JobEntry entry, string title)
    {
        Store.Rename(entry, string.IsNullOrWhiteSpace(title) ? entry.Title : title.Trim());
        RefreshHistory();
    }

    public void Delete(string id)
    {
        Store.Delete(id);
        if (Screen == Screen.Result && ResultId == id) Screen = Screen.Home;
        RefreshHistory();
    }

    public Transcript? LoadTranscript(string id)
    {
        try
        {
            return new Transcript(File.ReadAllText(Store.TranscriptPath(id)));
        }
        catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or NullReferenceException)
        {
            return null;
        }
    }

    public Transcript SaveSpeakerNames(string id, Transcript transcript, IReadOnlyDictionary<int, string> names)
    {
        var renamed = transcript.WithSpeakerNames(names);
        Store.SaveTranscript(id, renamed.Json);
        return renamed;
    }

    /// <summary>Asks where to save <paramref name="transcript"/> as <paramref name="format"/> and writes it.</summary>
    public async Task ExportAsync(JobEntry entry, Transcript transcript, string format)
    {
        if (_window is null) return;
        var picker = new FileSavePicker(_window.AppWindow.Id)
        {
            SuggestedFileName = entry.Title,
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
        };
        picker.FileTypeChoices.Add(format.ToUpperInvariant(), [$".{format}"]);
        var file = await picker.PickSaveFileAsync();
        if (file is null) return;
        var text = SillokLibrary.Export(transcript.Json, format, Strings.Get("SpeakerLabel"));
        await File.WriteAllTextAsync(file.Path, text);
    }

    // --- Settings -------------------------------------------------------------------

    private JobSettings LoadSettings()
    {
        try
        {
            return JsonSerializer.Deserialize(File.ReadAllText(_settingsPath), SettingsJson.Default.JobSettings) ?? new JobSettings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            return new JobSettings();
        }
    }

    private void SaveSettings()
    {
        Directory.CreateDirectory(DataDir);
        File.WriteAllText(_settingsPath, JsonSerializer.Serialize(Settings, SettingsJson.Default.JobSettings));
    }
}

[System.Text.Json.Serialization.JsonSerializable(typeof(JobSettings))]
internal sealed partial class SettingsJson : System.Text.Json.Serialization.JsonSerializerContext;
