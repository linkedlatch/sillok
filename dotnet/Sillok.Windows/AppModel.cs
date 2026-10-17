using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sillok.Core;
using Sillok.Core.Native;
using Sillok.Windows.Services;
using Windows.Storage;

namespace Sillok.Windows;

public enum ModelState
{
    Missing,
    Downloading,
    Ready,
    Failed,
}

public sealed record ModelRow(ModelEntry Entry, bool Installed)
{
    public string Name => Entry.DisplayName;
    public string Detail => $"{Entry.Size / 1_048_576}MB · {Entry.License}";
    public string StateText => Installed ? "설치됨" : "설치 안 됨";
    public Visibility RemoveVisibility => Installed ? Visibility.Visible : Visibility.Collapsed;
}

public sealed class AppModel : INotifyPropertyChanged
{
    private static readonly string Root = ApplicationData.Current.LocalFolder.Path;

    private readonly DispatcherQueue dispatcher = DispatcherQueue.GetForCurrentThread();
    private readonly JobQueue queue = new();
    private readonly NativeEngine engine = new();
    private readonly HashSet<Guid> deletionRequested = [];
    private readonly string settingsPath = Path.Combine(Root, "settings.json");
    private AppSettings settings;
    private ModelFiles? loadedFiles;
    private CancellationTokenSource? download;
    private ModelState modelState = ModelState.Missing;
    private string? modelError;
    private long downloadReceived;
    private long downloadTotal;
    private Guid? runningJob;
    private bool started;

    private AppModel()
    {
        settings = AppSettings.Load(settingsPath);
        ProjectStore = new ProjectStore(Path.Combine(Root, "Projects"));
        ModelStore = new ModelStore(Path.Combine(Root, "Models"), ModelManifest.Load(Path.Combine(AppContext.BaseDirectory, "manifest.json")));
        queue.Idle += () => _ = UnloadEngineAsync();
        Playback.StateChanged += () => dispatcher.TryEnqueue(() => Notify(nameof(PauseVisibility)));
    }

    public static AppModel Shared { get; } = new();

    public event PropertyChangedEventHandler? PropertyChanged;
    public event Action<string>? AlertRaised;

    public ProjectStore ProjectStore { get; }
    public ModelStore ModelStore { get; }
    public Playback Playback { get; } = new();
    public ObservableCollection<ProjectItem> Projects { get; } = [];
    public ObservableCollection<ModelRow> ModelRows { get; } = [];

    public bool SpeakerDiarization
    {
        get => settings.SpeakerDiarization;
        set => UpdateSettings(settings with { SpeakerDiarization = value });
    }

    public bool UseGpu
    {
        get => settings.UseGpu;
        set => UpdateSettings(settings with { UseGpu = value });
    }

    public string VocabularyText
    {
        get => settings.VocabularyText;
        set => UpdateSettings(settings with { VocabularyText = value });
    }

    public LanguageMode LanguageMode
    {
        get => settings.LanguageMode;
        set => UpdateSettings(settings with { LanguageMode = value });
    }

    public bool IsBusy => runningJob is not null || download is not null;
    public bool CanChangeModels => !IsBusy;

    public bool ShowModelBar => modelState != ModelState.Ready;
    public InfoBarSeverity ModelBarSeverity => modelState == ModelState.Failed ? InfoBarSeverity.Error : InfoBarSeverity.Informational;
    public string ModelBarTitle => modelState switch
    {
        ModelState.Downloading => "모델을 받는 중입니다",
        ModelState.Failed => "모델을 받지 못했습니다",
        _ => "전사에 필요한 모델이 없습니다",
    };
    public string ModelBarMessage => modelState switch
    {
        ModelState.Downloading => $"{downloadReceived / 1_048_576} / {downloadTotal / 1_048_576}MB",
        ModelState.Failed => modelError ?? "",
        _ => string.Join(", ", MissingModels.Select(entry => entry.DisplayName)) + $" ({MissingModels.Sum(entry => entry.Size) / 1_048_576}MB)",
    };
    public string ModelBarAction => modelState == ModelState.Downloading ? "취소" : "모델 받기";
    public double DownloadPercent => downloadTotal > 0 ? downloadReceived * 100.0 / downloadTotal : 0;
    public Visibility DownloadVisibility => modelState == ModelState.Downloading ? Visibility.Visible : Visibility.Collapsed;
    public Visibility PauseVisibility => Playback.IsPlaying ? Visibility.Visible : Visibility.Collapsed;

    private IReadOnlyList<ModelEntry> RequiredModels =>
        ModelSelection.RequiredEntries(settings.WhisperId, settings.SpeakerDiarization, ModelStore.Manifest);

    private IReadOnlyList<ModelEntry> MissingModels => RequiredModels.Where(entry => !ModelStore.IsInstalled(entry)).ToList();

    public void Start()
    {
        if (started) return;
        started = true;
        foreach (var project in ProjectStore.LoadAll()) Projects.Add(new ProjectItem(project));
        RefreshModelStatus();
    }

    public ProjectItem? Find(Guid id) => Projects.FirstOrDefault(item => item.Id == id);

    public void RefreshModelStatus()
    {
        ModelRows.Clear();
        foreach (var entry in ModelStore.Manifest.Models.Where(entry => entry.Kind != ModelKind.CoreMLEncoder))
        {
            ModelRows.Add(new ModelRow(entry, ModelStore.IsInstalled(entry)));
        }
        if (download is not null) return;
        var wasReady = modelState == ModelState.Ready;
        SetModelState(MissingModels.Count == 0 ? ModelState.Ready : ModelState.Missing);
        if (modelState == ModelState.Ready && !wasReady) EnqueueUnfinished();
    }

    public void ModelAction()
    {
        if (download is not null)
        {
            download.Cancel();
        }
        else
        {
            _ = DownloadModelsAsync();
        }
    }

    private async Task DownloadModelsAsync()
    {
        if (IsBusy) return;
        var entries = MissingModels;
        download = new CancellationTokenSource();
        downloadTotal = entries.Sum(entry => entry.Size);
        downloadReceived = 0;
        SetModelState(ModelState.Downloading);
        Exception? failure = null;
        try
        {
            var downloader = new ModelDownloader(ModelStore);
            long completed = 0;
            foreach (var entry in entries)
            {
                var start = completed;
                var progress = new Progress<long>(received =>
                {
                    downloadReceived = start + received;
                    Notify(nameof(ModelBarMessage));
                    Notify(nameof(DownloadPercent));
                });
                await downloader.DownloadAsync(entry, progress, download.Token);
                completed += entry.Size;
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception error)
        {
            failure = error;
        }
        download.Dispose();
        download = null;
        Notify(nameof(IsBusy));
        Notify(nameof(CanChangeModels));
        RefreshModelStatus();
        if (failure is not null && modelState != ModelState.Ready)
        {
            modelError = UserMessage.Describe(failure);
            SetModelState(ModelState.Failed);
        }
    }

    public void RemoveModel(string id)
    {
        if (IsBusy || ModelStore.Manifest.Entry(id) is not { } entry)
        {
            Alert("전사나 다운로드가 끝난 뒤에 다시 시도해 주세요");
            return;
        }
        _ = UnloadEngineAsync();
        ModelStore.Remove(entry);
        RefreshModelStatus();
    }

    public void AddFiles(IEnumerable<string> paths)
    {
        foreach (var path in paths)
        {
            var project = new TranscriptProject
            {
                SourceName = Path.GetFileName(path),
                SourcePath = path,
                Options = settings.TranscriptionOptions,
            };
            ProjectStore.Save(project);
            Projects.Insert(0, new ProjectItem(project));
            Enqueue(project.Id);
        }
    }

    public void Retry(Guid id)
    {
        Update(id, project => project with { Status = TranscriptStatus.Pending, FailureMessage = null });
        Enqueue(id);
    }

    public void Cancel(Guid id)
    {
        queue.Cancel(id);
        if (runningJob != id) Update(id, project => project with { Status = TranscriptStatus.Cancelled });
    }

    public void Delete(Guid id)
    {
        if (Find(id) is not { } item) return;
        if (string.Equals(Playback.CurrentPath, item.Project.SourcePath, StringComparison.OrdinalIgnoreCase)) Playback.Stop();
        Projects.Remove(item);
        if (runningJob == id)
        {
            deletionRequested.Add(id);
        }
        else
        {
            ProjectStore.Delete(id);
        }
        queue.Cancel(id);
    }

    public void Play(ProjectItem item, double seconds)
    {
        if (!File.Exists(item.Project.SourcePath))
        {
            Alert("원본 파일을 찾을 수 없습니다");
            return;
        }
        Playback.Play(item.Project.SourcePath, seconds);
    }

    private void EnqueueUnfinished()
    {
        foreach (var item in Projects.Reverse().Where(item => item.Project.Status is TranscriptStatus.Pending or TranscriptStatus.Transcribing))
        {
            Enqueue(item.Id);
        }
    }

    private void Enqueue(Guid id) => queue.Enqueue(id, cancellation => RunAsync(id, cancellation));

    private async Task RunAsync(Guid id, CancellationToken cancellation)
    {
        if (modelState != ModelState.Ready || Find(id) is not { } item || item.Project.Status == TranscriptStatus.Completed) return;
        if (ModelStore.Manifest.Entry(settings.WhisperId) is not { } whisper || ModelStore.Manifest.Entry(ModelSelection.VadId) is not { } vad) return;
        if (!File.Exists(item.Project.SourcePath))
        {
            Update(id, project => project with { Status = TranscriptStatus.Failed, FailureMessage = "원본 파일을 찾을 수 없습니다" });
            return;
        }

        SetRunning(id);
        using var awake = PowerRequest.KeepAwake();
        try
        {
            var audioPath = ProjectStore.AudioCachePath(id);
            if (!File.Exists(audioPath))
            {
                item.Progress = new TranscribeProgress(TranscribePhase.Analyzing, 0, 0, []);
                await AudioConverter.ConvertToWavAsync(item.Project.SourcePath, audioPath, cancellation);
            }
            using var source = new WavAudioSource(audioPath);
            await PrepareEngineAsync(whisper);
            Update(id, project => project with { Status = TranscriptStatus.Transcribing, Duration = source.Duration, ModelId = whisper.Id, Segments = [] });
            item.ReplaceRows([]);

            var diarizer = ModelStore.Manifest.Entry(ModelSelection.DiarizationId) is { } entry && ModelStore.IsInstalled(entry) ? ModelStore.Location(entry) : null;
            var pipeline = new FileTranscriptionPipeline(
                new NativeCore(engine, ModelStore.Location(vad), diarizer), item.Project.Options, whisper.Id, ProjectStore.CheckpointPath(id));
            await foreach (var step in pipeline.RunAsync(source, cancellation))
            {
                item.Progress = step;
                if (step.ReplacesSegments)
                {
                    Update(id, project => project with { Segments = step.NewSegments });
                    item.ReplaceRows(step.NewSegments);
                }
                else if (step.NewSegments.Count > 0)
                {
                    Update(id, project => project with { Segments = [.. project.Segments, .. step.NewSegments] }, save: false);
                    item.AppendRows(step.NewSegments);
                }
            }
            cancellation.ThrowIfCancellationRequested();
            Update(id, project => project with { Status = TranscriptStatus.Completed });
            File.Delete(ProjectStore.CheckpointPath(id));
            File.Delete(audioPath);
        }
        catch (OperationCanceledException)
        {
            Update(id, project => project with { Status = TranscriptStatus.Cancelled });
        }
        catch (Exception error)
        {
            Update(id, project => project with { Status = TranscriptStatus.Failed, FailureMessage = UserMessage.Describe(error) });
        }
        finally
        {
            item.Progress = null;
            SetRunning(null);
            if (deletionRequested.Remove(id)) ProjectStore.Delete(id);
        }
    }

    private async Task PrepareEngineAsync(ModelEntry whisper)
    {
        if (engine.LoadedFiles is null) loadedFiles = null;
        var files = new ModelFiles(ModelStore.Location(whisper));
        if (loadedFiles == files) return;
        loadedFiles = null;
        await engine.LoadAsync(files, settings.UseGpu);
        loadedFiles = files;
    }

    private async Task UnloadEngineAsync()
    {
        if (runningJob is not null) return;
        loadedFiles = null;
        await engine.UnloadAsync();
    }

    private void Update(Guid id, Func<TranscriptProject, TranscriptProject> change, bool save = true)
    {
        if (Find(id) is not { } item) return;
        item.Project = change(item.Project) with { UpdatedAt = DateTimeOffset.Now };
        if (save) ProjectStore.Save(item.Project);
    }

    private void UpdateSettings(AppSettings updated)
    {
        if (updated == settings) return;
        settings = updated;
        settings.Save(settingsPath);
        RefreshModelStatus();
    }

    private void SetRunning(Guid? id)
    {
        runningJob = id;
        Notify(nameof(IsBusy));
        Notify(nameof(CanChangeModels));
    }

    private void SetModelState(ModelState state)
    {
        modelState = state;
        foreach (var name in new[] { nameof(ShowModelBar), nameof(ModelBarSeverity), nameof(ModelBarTitle), nameof(ModelBarMessage), nameof(ModelBarAction), nameof(DownloadVisibility), nameof(IsBusy), nameof(CanChangeModels) })
        {
            Notify(name);
        }
    }

    private void Alert(string message) => AlertRaised?.Invoke(message);

    private void Notify([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
