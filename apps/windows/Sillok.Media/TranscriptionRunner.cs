using System.Diagnostics;

namespace Sillok.Media;

/// <summary>
/// Runs one transcription: decodes the file, pushes its audio to the library
/// while writing the listening copy, runs the library and stores the result
/// in a new job folder. Single use; <see cref="Cancel"/> may be called from
/// any thread.
/// </summary>
public sealed class TranscriptionRunner(JobStore store, string modelsDir, string tempDir)
{
    private const int CopyBlock = SillokLibrary.SampleRate;  // 1 s per read from the job
    private volatile bool _cancelled;
    private Job? _job;

    /// <summary>
    /// Where the time of the read step goes, in milliseconds: <see cref="CopyMs"/>
    /// is how long the listening copy held decoding up.
    /// </summary>
    public sealed record ReadTimings(long TotalMs, long PushMs, long CopyMs)
    {
        public long DecodeMs => TotalMs - PushMs - CopyMs;
    }

    public void Cancel()
    {
        _cancelled = true;
        Volatile.Read(ref _job)?.Cancel();
    }

    /// <summary>Blocks until done. Returns the stored job, or null if cancelled.</summary>
    public JobEntry? Run(string path, string source, JobSettings settings, Action<RunStep, double> onProgress,
        Action<TranscriptionRunner.ReadTimings>? onRead = null, Action<IReadOnlyList<Transcript.Segment>>? onSegments = null)
    {
        var dir = store.Create();
        JobEntry? entry = null;
        // Wall time of each step, kept with the job to compare devices.
        var timings = new Dictionary<string, long>();
        RunStep? current = null;
        var clock = Stopwatch.StartNew();
        void Enter(RunStep? step)
        {
            if (current is { } previous)
            {
                var key = previous.ToString().ToLowerInvariant();
                timings[key] = timings.GetValueOrDefault(key) + clock.ElapsedMilliseconds;
            }
            current = step;
            clock.Restart();
        }
        void Report(RunStep step, double fraction)
        {
            if (step != current) Enter(step);
            onProgress(step, fraction);
        }

        try
        {
            using var engine = new Engine(modelsDir, tempDir, settings.GpuIndex, transcriptionModel: settings.TranscriptionModel);
            using var job = new Job(engine, settings.Language, settings.Diarize, settings.Terms);
            Volatile.Write(ref _job, job);
            try
            {
                if (_cancelled) return null;
                Report(RunStep.Read, 0);
                var read = Read(path, job, Path.Combine(dir, JobStore.AudioFile), Report);
                if (_cancelled) return null;
                onRead?.Invoke(read);
                string transcript;
                try
                {
                    transcript = job.Run((stage, fraction) => Report(JobSettings.ToStep(stage), fraction), onSegments);
                }
                catch (SillokException e) when (e.IsCancelled)
                {
                    return null;
                }
                Enter(null);
                timings["read_decode"] = read.DecodeMs;
                timings["read_push"] = read.PushMs;
                timings["read_copy"] = read.CopyMs;
                entry = store.Complete(dir, transcript, source, new Transcript(transcript).DurationMs,
                    new JobStats(timings, Environment.ProcessorCount, "Media Foundation"));
                return entry;
            }
            finally
            {
                Volatile.Write(ref _job, null);
            }
        }
        finally
        {
            if (entry is null) Directory.Delete(dir, recursive: true);
        }
    }

    /// <summary>
    /// Decodes the file into <paramref name="job"/> and writes the listening
    /// copy from the job's own 16 kHz recording, read back as it grows: the
    /// copy then holds exactly what the transcript's times refer to.
    /// </summary>
    public ReadTimings Read(string path, Job job, string copyPath, Action<RunStep, double> onProgress)
    {
        var total = Stopwatch.StartNew();
        var push = new Stopwatch();
        var copy = new Stopwatch();
        (int Rate, int Channels)? format = null;
        long copied = 0;
        using var writer = new CopyWriter(copyPath);

        void CopyNewAudio(bool all)
        {
            var length = job.AudioLength;
            if (!all && length - copied < CopyBlock) return;
            copy.Start();
            while (copied < length)
            {
                var block = new float[Math.Min(length - copied, CopyBlock)];
                var read = job.ReadAudio(copied, block);
                writer.Write(read == block.Length ? block : block[..read]);
                copied += read;
            }
            copy.Stop();
        }

        AudioDecoder.Decode(path, () => _cancelled, f => onProgress(RunStep.Read, f), (samples, rate, channels) =>
        {
            format ??= (rate, channels);
            if (format != (rate, channels)) throw new UnsupportedMediaException("audio format changes within the file");
            push.Start();
            job.PushAudio(samples, rate, channels);
            push.Stop();
            CopyNewAudio(all: false);
        });
        if (!_cancelled)
        {
            if (format is null) throw new UnsupportedMediaException("the file has no audio");
            job.EndAudio();
            CopyNewAudio(all: true);
            copy.Start();
            writer.Finish();
            copy.Stop();
        }
        return new ReadTimings(total.ElapsedMilliseconds, push.ElapsedMilliseconds, copy.ElapsedMilliseconds);
    }
}
