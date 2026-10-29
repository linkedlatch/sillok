using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.Storage.Pickers;
using Sillok.Media;

namespace Sillok.App;

/// <summary>Whether a catalog model is on this computer.</summary>
public abstract record ModelStatus
{
    public sealed record Ready : ModelStatus;
    public sealed record Missing(long Downloaded) : ModelStatus;
    public sealed record Downloading(long Done, long Total, long BytesPerSecond) : ModelStatus;
    public sealed record Verifying : ModelStatus;
}

/// <summary>A file in the queue: waiting, or failed with <see cref="Error"/> until retried or removed.</summary>
public sealed record QueueItem(string Key, string Path, string Name, JobSettings Settings, string? Error = null);

/// <summary>Progress of the running transcription: the current step, its fraction and when the job and the step began.</summary>
public sealed record RunInfo(
    string Key, string Name, IReadOnlyList<RunStep> Steps, RunStep Step, double Fraction, long StartedMs, long StepStartedMs,
    bool Cancelling = false, TranscriptionRunner.ReadTimings? Read = null)
{
    /// <summary>The whole job's fraction done; the steps weigh roughly what they take on a CPU.</summary>
    public double Overall
    {
        get
        {
            var total = Steps.Sum(Weight);
            var before = Steps.TakeWhile(s => s != Step).Sum(Weight);
            return Math.Clamp((before + Weight(Step) * Fraction) / total, 0, 1);
        }
    }

    /// <summary>Time left for the whole job, extrapolated once enough is done to tell; null before.</summary>
    public long? RemainingMs(long nowMs)
    {
        var elapsed = nowMs - StartedMs;
        var done = Overall;
        if (Cancelling || done < 0.03 || elapsed < 20_000) return null;
        return (long)(elapsed * (1 - done) / done);
    }

    private static double Weight(RunStep step) => step switch
    {
        RunStep.Read => 0.03,
        RunStep.Analyze => 0.02,
        RunStep.Transcribe => 0.85,
        _ => 0.10,
    };
}

/// <summary>One row of the library: a running, waiting, failed or finished transcription.</summary>
public abstract record LibraryItem(string Key)
{
    public sealed record Running(RunInfo Run) : LibraryItem(Run.Key);
    public sealed record Waiting(QueueItem Item, int Position) : LibraryItem(Item.Key);
    public sealed record Failed(QueueItem Item) : LibraryItem(Item.Key);
    public sealed record Done(JobEntry Entry) : LibraryItem(Entry.Id);
}

/// <summary>
/// The app's state and actions, shared by every page, as in the Android app:
/// a library of transcriptions, a saved queue worked through one file at a
/// time, the models and the defaults. Pages listen to PropertyChanged; work
/// runs off the UI thread and reports back through the window's dispatcher.
/// </summary>
public sealed partial class AppState : ObservableObject, IDisposable
{
    public static string DataDir { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Sillok");

    private readonly string _modelsDir = Path.Combine(DataDir, "models");
    private readonly string _settingsPath = Path.Combine(DataDir, "settings.json");
    private readonly string _queuePath = Path.Combine(DataDir, "queue.json");
    private readonly HttpClient _http = new();
    private readonly ModelDownloader _downloader;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private DispatcherQueue? _ui;
    private Window? _window;
    private CancellationTokenSource? _download;
    private TranscriptionRunner? _runner;
    private bool _working;

    public AppState()
    {
        Store = new JobStore(Path.Combine(DataDir, "jobs"));
        _downloader = new ModelDownloader(_modelsDir, _http);
        Catalog = SillokLibrary.Catalog();
        Defaults = LoadSettings();
        Queue = LoadQueue();
        Models = new Dictionary<string, ModelStatus>();
        History = [];
        Previews = [];
    }

    public JobStore Store { get; }
    public IReadOnlyList<CatalogModel> Catalog { get; }
    public IReadOnlyList<ComputeDevice> Gpus { get; private set; } = [];
    public static string Version => $"{typeof(AppState).Assembly.GetName().Version}\n{SillokLibrary.Version}";
    public long Now => _clock.ElapsedMilliseconds;

    [ObservableProperty] public partial IReadOnlyDictionary<string, ModelStatus> Models { get; set; }
    /// <summary>False until the first check of the model files is done.</summary>
    [ObservableProperty] public partial bool ModelsChecked { get; set; }
    [ObservableProperty] public partial IReadOnlyList<JobEntry> History { get; set; }
    [ObservableProperty] public partial IReadOnlyList<QueueItem> Queue { get; set; }
    [ObservableProperty] public partial RunInfo? Run { get; set; }
    /// <summary>Segments of the running job so far, as the transcription finishes them.</summary>
    [ObservableProperty] public partial IReadOnlyList<Transcript.Segment> Previews { get; set; }
    [ObservableProperty] public partial JobSettings Defaults { get; set; }
    /// <summary>The last file that finished: its queue key and its job id, so that a page showing it can follow.</summary>
    [ObservableProperty] public partial (string Key, string Id)? Finished { get; set; }
    [ObservableProperty] public partial bool ShowSettings { get; set; }
    /// <summary>A message to show once; the window clears it.</summary>
    [ObservableProperty] public partial string? Message { get; set; }

    /// <summary>The library: the running job, waiting and failed files, then the history.</summary>
    public IReadOnlyList<LibraryItem> Library
    {
        get
        {
            var items = new List<LibraryItem>();
            if (Run is { } run) items.Add(new LibraryItem.Running(run));
            var position = 0;
            foreach (var item in Queue)
            {
                if (item.Key == Run?.Key) continue;
                items.Add(item.Error is not null ? new LibraryItem.Failed(item) : new LibraryItem.Waiting(item, ++position));
            }
            items.AddRange(History.Select(e => new LibraryItem.Done(e)));
            return items;
        }
    }

    public void Attach(Window window)
    {
        _window = window;
        _ui = window.DispatcherQueue;
        Directory.CreateDirectory(DataDir);
        Store.RemoveIncomplete();
        Gpus = SillokLibrary.Devices().Where(d => d.IsGpu).ToList();
        // A GPU when there is one (docs/DESIGN.md D15: the app chooses).
        if (!File.Exists(_settingsPath) && Gpus.Count > 0) Defaults = Defaults with { GpuIndex = 0 };
        if (Defaults.GpuIndex >= Gpus.Count) Defaults = Defaults with { GpuIndex = null };
        RefreshHistory();
        _ = CheckModelsAsync();
        Resume();
    }

    private void OnUi(Action action) => _ui?.TryEnqueue(() => action());

    // --- Models --------------------------------------------------------------

    public IReadOnlyList<CatalogModel> TranscriptionModels => Catalog.Where(m => m.Role == "transcription").ToList();
    public CatalogModel RecommendedTranscription => TranscriptionModels.First(m => m.Recommended);
    public IReadOnlyList<CatalogModel> ReadyTranscriptionModels => TranscriptionModels.Where(m => Models.GetValueOrDefault(m.Id) is ModelStatus.Ready).ToList();

    /// <summary>Whether a job can run with <paramref name="transcriptionModel"/> (null: the recommended one).</summary>
    public bool ModelsReady(string? transcriptionModel) =>
        SillokLibrary.EngineModels(transcriptionModel).All(m => Models.GetValueOrDefault(m.Id) is ModelStatus.Ready);

    private async Task CheckModelsAsync()
    {
        var status = await Task.Run(() => SillokLibrary.Check(_modelsDir, verifyHash: false));
        Models = Catalog.ToDictionary(m => m.Id, m =>
        {
            var files = m.Files.Where(f => !f.Zip).ToList();
            return files.All(f => status.GetValueOrDefault(f.File) == "ok")
                ? (ModelStatus)new ModelStatus.Ready()
                : new ModelStatus.Missing(_downloader.Downloaded(files.Where(f => status.GetValueOrDefault(f.File) != "ok")));
        });
        ModelsChecked = true;
    }

    /// <summary>Downloads <paramref name="modelId"/> with the other roles' models it needs. One download at a time.</summary>
    public async Task DownloadModelAsync(string modelId)
    {
        if (_download is not null) return;
        _download = new CancellationTokenSource();
        var model = Catalog.First(m => m.Id == modelId);
        var files = await Task.Run(() => _downloader.Missing(model.Role == "transcription" ? model.Id : null));
        var total = files.Sum(f => f.Size);
        long last = _downloader.Downloaded(files);
        var lastAt = Now;
        SetModel(modelId, new ModelStatus.Downloading(last, total, 0));
        var progress = new Progress<long>(done =>
        {
            // Progress<T> posts to the UI thread it was created on.
            var now = Now;
            var speed = now > lastAt ? (done - last) * 1000 / (now - lastAt) : 0;
            if (now - lastAt >= 500)
            {
                last = done;
                lastAt = now;
            }
            SetModel(modelId, done >= total ? new ModelStatus.Verifying() : new ModelStatus.Downloading(done, total, speed));
        });
        try
        {
            var token = _download.Token;
            await Task.Run(() => _downloader.DownloadAsync(files, progress, token), token);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ModelIntegrityException)
        {
            Message = Strings.Get("ModelsDamaged");
        }
        catch (Exception e) when (e is IOException or HttpRequestException)
        {
            Message = Strings.Format("ModelsDownloadFailed", e.Message);
        }
        finally
        {
            _download.Dispose();
            _download = null;
            await CheckModelsAsync();
            Resume();
        }
    }

    private void SetModel(string id, ModelStatus status) =>
        Models = new Dictionary<string, ModelStatus>(Models) { [id] = status };

    public void StopDownload() => _download?.Cancel();

    /// <summary>Removes a transcription model's files; not the one the defaults use.</summary>
    public async Task DeleteModelAsync(string modelId)
    {
        if (modelId == (Defaults.TranscriptionModel ?? RecommendedTranscription.Id)) return;
        var keep = Catalog.Where(m => m.Id != modelId && Models.GetValueOrDefault(m.Id) is ModelStatus.Ready)
            .SelectMany(m => m.Files).Select(f => f.File).ToHashSet();
        await Task.Run(() =>
        {
            foreach (var file in Catalog.First(m => m.Id == modelId).Files.Where(f => !keep.Contains(f.File)))
            {
                File.Delete(Path.Combine(_modelsDir, file.File));
            }
        });
        await CheckModelsAsync();
    }

    /// <summary>Re-reads every model file and compares its hash with the catalog.</summary>
    public async Task VerifyModelsAsync()
    {
        if (Run is not null || _download is not null) return;
        try
        {
            await Task.Run(_downloader.Verify);
            Message = Strings.Get("ModelsVerified");
        }
        catch (ModelIntegrityException)
        {
            Message = Strings.Get("ModelsDamaged");
        }
        await CheckModelsAsync();
    }

    /// <summary>Bytes the models and the history take.</summary>
    public (long Models, long History) StorageUse()
    {
        static long Size(string dir) => Directory.Exists(dir)
            ? new DirectoryInfo(dir).EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length)
            : 0;
        return (Size(_modelsDir), Size(Path.Combine(DataDir, "jobs")));
    }

    // --- Adding files ----------------------------------------------------------

    // Types Media Foundation decodes on a standard Windows; "*" lets users try others.
    private static readonly string[] MediaFileTypes =
        [".m4a", ".mp3", ".wav", ".aac", ".wma", ".flac", ".mp4", ".mov", ".m4v", ".wmv", ".3gp", "*"];

    /// <summary>Asks for files to transcribe; null when none were picked.</summary>
    public async Task<IReadOnlyList<string>?> PickFilesAsync()
    {
        if (_window is null) return null;
        var picker = new FileOpenPicker(_window.AppWindow.Id) { SuggestedStartLocation = PickerLocationId.MusicLibrary };
        foreach (var type in MediaFileTypes) picker.FileTypeFilter.Add(type);
        var files = await picker.PickMultipleFilesAsync();
        return files is { Count: > 0 } ? files.Select(f => f.Path).ToList() : null;
    }

    /// <summary>Queues <paramref name="paths"/> with <paramref name="settings"/> and starts on them.</summary>
    public void Enqueue(IReadOnlyList<string> paths, JobSettings settings)
    {
        UpdateQueue(q => [.. q, .. paths.Select(p => new QueueItem(Guid.NewGuid().ToString(), p, Path.GetFileName(p), settings))]);
        Resume();
    }

    public void SaveDefaults(JobSettings settings)
    {
        Defaults = settings;
        Directory.CreateDirectory(DataDir);
        File.WriteAllText(_settingsPath, JsonSerializer.Serialize(settings, AppJson.Default.JobSettings));
    }

    // --- Queue --------------------------------------------------------------------

    /// <summary>Works through the waiting files.</summary>
    public void Resume()
    {
        if (_working || !Queue.Any(q => q.Error is null)) return;
        _working = true;
        _ = Task.Run(Work);
    }

    public void Retry(string key)
    {
        UpdateQueue(q => q.Select(i => i.Key == key ? i with { Error = null } : i).ToList());
        Resume();
    }

    /// <summary>Removes a waiting or failed file; the running one is cancelled.</summary>
    public void Remove(string key)
    {
        if (Run?.Key == key)
        {
            Cancel();
            return;
        }
        UpdateQueue(q => q.Where(i => i.Key != key).ToList());
    }

    public void Cancel()
    {
        if (Run is not { } run) return;
        Run = run with { Cancelling = true };
        _runner?.Cancel();
    }

    private void Work()
    {
        while (true)
        {
            var item = Queue.FirstOrDefault(q => q.Error is null);
            if (item is null)
            {
                OnUi(() =>
                {
                    _working = false;
                    Run = null;
                    if (Queue.Any(q => q.Error is null)) Resume();
                });
                return;
            }
            var steps = new List<RunStep> { RunStep.Read, RunStep.Analyze, RunStep.Transcribe };
            if (item.Settings.Diarize) steps.Add(RunStep.Diarize);
            var runner = new TranscriptionRunner(Store, _modelsDir, Path.GetTempPath());
            var started = Now;
            OnUi(() =>
            {
                _runner = runner;
                Previews = [];
                Run = new RunInfo(item.Key, item.Name, steps, RunStep.Read, 0, started, started);
            });
            var lastStep = RunStep.Read;
            var lastFraction = 0.0;
            JobEntry? entry = null;
            string? failure = null;
            try
            {
                entry = runner.Run(item.Path, item.Name, item.Settings, (step, fraction) =>
                {
                    // Called often from the worker; pass on visible changes only.
                    if (step == lastStep && fraction - lastFraction < 0.001 && fraction < 1) return;
                    lastStep = step;
                    lastFraction = fraction;
                    OnUi(() => Progress(step, fraction));
                },
                read => OnUi(() => { if (Run is { } r) Run = r with { Read = read }; }),
                segments => OnUi(() => Previews = [.. Previews, .. segments]));
            }
            catch (Exception e)
            {
                failure = Describe(e);
            }
            using var finished = new ManualResetEventSlim();
            OnUi(() =>
            {
                _runner = null;
                Previews = [];
                if (entry is not null)
                {
                    UpdateQueue(q => q.Where(i => i.Key != item.Key).ToList());
                    RefreshHistory();
                    Finished = (item.Key, entry.Id);
                }
                else if (failure is not null)
                {
                    UpdateQueue(q => q.Select(i => i.Key == item.Key ? i with { Error = failure } : i).ToList());
                }
                else
                {
                    UpdateQueue(q => q.Where(i => i.Key != item.Key).ToList());
                }
                finished.Set();
            });
            finished.Wait();
        }
    }

    private void Progress(RunStep step, double fraction)
    {
        if (Run is not { } run) return;
        Run = step != run.Step ? run with { Step = step, Fraction = fraction, StepStartedMs = Now } : run with { Fraction = fraction };
    }

    private static string Describe(Exception e) => e switch
    {
        UnsupportedMediaException => Strings.Format("ErrorUnsupportedFile", e.Message),
        SillokException { IsModelError: true } => Strings.Format("ErrorModel", e.Message),
        _ => Strings.Format("ErrorEngine", e.Message),
    };

    private void UpdateQueue(Func<IReadOnlyList<QueueItem>, IReadOnlyList<QueueItem>> change)
    {
        Queue = change(Queue);
        Directory.CreateDirectory(DataDir);
        var temp = _queuePath + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(Queue.ToList(), AppJson.Default.ListQueueItem));
        File.Move(temp, _queuePath, overwrite: true);
    }

    private List<QueueItem> LoadQueue()
    {
        try
        {
            return JsonSerializer.Deserialize(File.ReadAllText(_queuePath), AppJson.Default.ListQueueItem) ?? [];
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            return [];
        }
    }

    // --- History ------------------------------------------------------------------

    public void RefreshHistory() => History = Store.List();

    public JobEntry? Entry(string id) => History.FirstOrDefault(e => e.Id == id);

    public void Rename(JobEntry entry, string title)
    {
        Store.Rename(entry, string.IsNullOrWhiteSpace(title) ? entry.Title : title.Trim());
        RefreshHistory();
    }

    public void Delete(string id)
    {
        Store.Delete(id);
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
        var path = await PickSaveAsync(entry.Title, format);
        if (path is null) return;
        await File.WriteAllTextAsync(path, SillokLibrary.Export(transcript.Json, format, Strings.Get("SpeakerLabel")));
    }

    /// <summary>Asks where to save the listening copy and copies it there.</summary>
    public async Task ExportRecordingAsync(JobEntry entry)
    {
        var source = Store.AudioPath(entry.Id);
        if (!File.Exists(source)) return;
        var path = await PickSaveAsync(entry.Title, "m4a");
        if (path is not null) File.Copy(source, path, overwrite: true);
    }

    private async Task<string?> PickSaveAsync(string name, string extension)
    {
        if (_window is null) return null;
        var picker = new FileSavePicker(_window.AppWindow.Id)
        {
            SuggestedFileName = name,
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
        };
        picker.FileTypeChoices.Add(extension.ToUpperInvariant(), [$".{extension}"]);
        return (await picker.PickSaveFileAsync())?.Path;
    }

    // --- Settings ---------------------------------------------------------------------

    private JobSettings LoadSettings()
    {
        try
        {
            return JsonSerializer.Deserialize(File.ReadAllText(_settingsPath), AppJson.Default.JobSettings) ?? new JobSettings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            return new JobSettings();
        }
    }

    public void Dispose()
    {
        _download?.Dispose();
        _http.Dispose();
        GC.SuppressFinalize(this);
    }

    partial void OnRunChanged(RunInfo? value) => OnPropertyChanged(nameof(Library));
    partial void OnQueueChanged(IReadOnlyList<QueueItem> value) => OnPropertyChanged(nameof(Library));
    partial void OnHistoryChanged(IReadOnlyList<JobEntry> value) => OnPropertyChanged(nameof(Library));
}

[JsonSerializable(typeof(JobSettings))]
[JsonSerializable(typeof(List<QueueItem>))]
internal sealed partial class AppJson : JsonSerializerContext;
