using Sillok.Media;

namespace Sillok.Tests;

public class MediaTests
{
    /// <summary>A copy written from 16 kHz decodes back to the same length at 48 kHz.</summary>
    [Fact]
    public void CopyDecodesToTheSameLength()
    {
        var path = Path.Combine(Directory.CreateTempSubdirectory("sillok-media").FullName, "copy.m4a");
        var upsampler = new Upsampler();
        var tone = Enumerable.Range(0, 3 * SillokLibrary.SampleRate)
            .Select(i => (float)Math.Sin(2 * Math.PI * 440 * i / SillokLibrary.SampleRate) * 0.4f).ToArray();
        using (var writer = new AacFileWriter(path))
        {
            writer.Write(ToPcm16(upsampler.Process(tone)));
            writer.Write(ToPcm16(upsampler.Finish()));
            Assert.True(writer.Finish());
        }
        Assert.InRange(new FileInfo(path).Length, 20_000, 60_000);  // 3 s at 96 kbit/s is about 36 kB

        long frames = 0;
        var rate = 0;
        AudioDecoder.Decode(path, () => false, _ => { }, (samples, sampleRate, channels) =>
        {
            rate = sampleRate;
            frames += samples.Length / channels;
        });
        Assert.Equal(AacFileWriter.SampleRate, rate);
        // AAC adds encoder delay and pads the last frame, well under the half
        // second the transcript tolerates.
        Assert.InRange(frames / (double)rate, 2.9, 3.25);
    }

    [Fact]
    public void RejectsAFileWithoutAudio()
    {
        var path = Path.Combine(Directory.CreateTempSubdirectory("sillok-media").FullName, "not-audio.txt");
        File.WriteAllText(path, "hello");
        Assert.Throws<UnsupportedMediaException>(() => AudioDecoder.Decode(path, () => false, _ => { }, (_, _, _) => { }));
    }

    /// <summary>The app's whole job with the real models: decode, copy, transcribe, store.</summary>
    [Fact]
    public void TranscribesAFileIntoTheHistory()
    {
        var models = Environment.GetEnvironmentVariable("SILLOK_MODELS");
        var speech = Environment.GetEnvironmentVariable("SILLOK_SPEECH");
        Assert.SkipWhen(string.IsNullOrEmpty(models) || string.IsNullOrEmpty(speech), "SILLOK_MODELS and SILLOK_SPEECH are not set");
        var store = new JobStore(Directory.CreateTempSubdirectory("sillok-jobs").FullName);
        var steps = new List<RunStep>();
        var runner = new TranscriptionRunner(store, models!, Path.GetTempPath());
        var entry = runner.Run(speech!, "speech.wav", new JobSettings(Language: "ko", Diarize: true), (step, _) =>
        {
            if (steps.Count == 0 || steps[^1] != step) steps.Add(step);
        });
        Assert.NotNull(entry);
        Assert.Equal([RunStep.Read, RunStep.Analyze, RunStep.Transcribe, RunStep.Diarize], steps);
        var transcript = new Transcript(File.ReadAllText(store.TranscriptPath(entry.Id)));
        var text = string.Join(" ", transcript.Segments.Select(s => s.Text));
        Assert.True(text.Any(c => c is >= '가' and <= '힣'), text);
        Assert.True(new FileInfo(store.AudioPath(entry.Id)).Length > 1_000);
        TestContext.Current.SendDiagnosticMessage($"timings {string.Join(", ", entry.Stats!.TimingsMs)}; transcript: {text}");
    }

    private static short[] ToPcm16(float[] samples) =>
        samples.Select(s => (short)Math.Clamp(MathF.Round(s * 32768f), -32768f, 32767f)).ToArray();
}
