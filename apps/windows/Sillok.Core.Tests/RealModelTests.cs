using System.Buffers.Binary;

namespace Sillok.Tests;

/// <summary>A job with the real models, as the app runs one. Needs SILLOK_MODELS and SILLOK_SPEECH.</summary>
public class RealModelTests
{
    [Fact]
    public void TranscribesPushedAudioAndKeepsItsRecording()
    {
        var models = Environment.GetEnvironmentVariable("SILLOK_MODELS");
        var speech = Environment.GetEnvironmentVariable("SILLOK_SPEECH");
        Assert.SkipWhen(string.IsNullOrEmpty(models) || string.IsNullOrEmpty(speech), "SILLOK_MODELS and SILLOK_SPEECH are not set");
        var (samples, rate, channels) = ReadWav(speech!);

        using var engine = new Engine(models!, Path.GetTempPath(), null);
        using var job = new Job(engine, "ko", diarize: false, []);
        var copy = new List<float>();
        var buffer = new float[SillokLibrary.SampleRate];
        void CopyNew()
        {
            int read;
            while ((read = job.ReadAudio(copy.Count, buffer)) > 0) copy.AddRange(buffer.AsSpan(0, read));
        }
        const int block = 4096;
        for (var start = 0; start < samples.Length; start += block * channels)
        {
            job.PushAudio(samples.AsSpan(start, Math.Min(block * channels, samples.Length - start)), rate, channels);
            CopyNew();
        }
        job.EndAudio();
        CopyNew();
        var frames = samples.Length / channels;
        Assert.Equal((long)frames * SillokLibrary.SampleRate / rate, copy.Count, tolerance: 1.0);
        Assert.Equal(copy.Count, job.AudioLength);

        var stages = new List<(Stage, double)>();
        var previews = new List<Transcript.Segment>();
        var transcript = new Transcript(job.Run((stage, fraction) => stages.Add((stage, fraction)), previews.AddRange));
        Assert.NotEmpty(previews);
        Assert.All(previews, s => Assert.Equal("ko", s.Language));
        Assert.Equal((Stage.Analyze, 0.0), stages[0]);
        Assert.Contains((Stage.Transcribe, 0.0), stages);
        var text = string.Join(" ", transcript.Segments.Select(s => s.Text));
        Assert.True(text.Any(c => c is >= '가' and <= '힣'), text);
        TestContext.Current.SendDiagnosticMessage($"transcript: {text}");
    }

    /// <summary>A WAV of 16-bit integer or 32-bit float samples, as floats in [-1, 1].</summary>
    private static (float[] Samples, int Rate, int Channels) ReadWav(string path)
    {
        var bytes = File.ReadAllBytes(path);
        int rate = 0, channels = 0, bits = 0, offset = 12;
        while (offset + 8 <= bytes.Length)
        {
            var id = System.Text.Encoding.ASCII.GetString(bytes, offset, 4);
            var size = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(offset + 4));
            var body = bytes.AsSpan(offset + 8, Math.Min(size, bytes.Length - offset - 8));
            if (id == "fmt ")
            {
                channels = BinaryPrimitives.ReadInt16LittleEndian(body[2..]);
                rate = BinaryPrimitives.ReadInt32LittleEndian(body[4..]);
                bits = BinaryPrimitives.ReadInt16LittleEndian(body[14..]);
                Assert.True(bits is 16 or 32, $"{bits}-bit samples");  // FLEURS ships 32-bit float
            }
            else if (id == "data")
            {
                var bytesPerSample = bits / 8;
                var samples = new float[body.Length / bytesPerSample];
                for (var i = 0; i < samples.Length; i++)
                {
                    var at = body[(bytesPerSample * i)..];
                    samples[i] = bits == 16 ? BinaryPrimitives.ReadInt16LittleEndian(at) / 32768f : BinaryPrimitives.ReadSingleLittleEndian(at);
                }
                return (samples, rate, channels);
            }
            offset += 8 + size + (size & 1);
        }
        throw new InvalidDataException("no data chunk");
    }
}
