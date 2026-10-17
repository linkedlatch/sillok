using System.Text;

namespace Sillok.Core;

public static class PcmFormat
{
    public const int SampleRate = 16000;
}

public interface IAudioSource
{
    double Duration { get; }
    float[] Read(double start, double end);
}

public sealed class ArrayAudioSource(float[] samples) : IAudioSource
{
    public double Duration => samples.Length / (double)PcmFormat.SampleRate;

    public float[] Read(double start, double end)
    {
        var lower = (int)Math.Clamp(Math.Round(start * PcmFormat.SampleRate), 0, samples.Length);
        var upper = (int)Math.Clamp(Math.Round(end * PcmFormat.SampleRate), 0, samples.Length);
        return lower < upper ? samples[lower..upper] : [];
    }
}

public sealed class WavAudioSource : IAudioSource, IDisposable
{
    private const int PcmCode = 1;
    private const int FloatCode = 3;
    private const int ExtensibleCode = 0xFFFE;

    private readonly FileStream stream;
    private readonly object gate = new();
    private readonly long dataOffset;
    private readonly long sampleCount;
    private readonly bool isFloat;

    public WavAudioSource(string path)
    {
        stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        try
        {
            using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: true);
            if (Tag(reader) != "RIFF") throw new InvalidDataException("not a WAV file");
            reader.ReadUInt32();
            if (Tag(reader) != "WAVE") throw new InvalidDataException("not a WAV file");

            int format = 0, channels = 0, rate = 0, bits = 0;
            long dataLength = -1;
            while (stream.Position + 8 <= stream.Length)
            {
                var id = Tag(reader);
                var size = reader.ReadUInt32();
                var next = stream.Position + size + (size & 1);
                if (id == "fmt ")
                {
                    format = reader.ReadUInt16();
                    channels = reader.ReadUInt16();
                    rate = reader.ReadInt32();
                    reader.ReadInt32();
                    reader.ReadUInt16();
                    bits = reader.ReadUInt16();
                    if (format == ExtensibleCode && size >= 40)
                    {
                        reader.ReadUInt16();
                        reader.ReadUInt16();
                        reader.ReadUInt32();
                        format = reader.ReadUInt16();
                    }
                }
                else if (id == "data")
                {
                    dataOffset = stream.Position;
                    dataLength = Math.Min(size, stream.Length - dataOffset);
                    break;
                }
                stream.Position = next;
            }

            if (dataLength < 0) throw new InvalidDataException("WAV file has no data");
            if (rate != PcmFormat.SampleRate || channels != 1) throw new InvalidDataException($"WAV is not 16 kHz mono ({rate} Hz, {channels} channels)");
            isFloat = format == FloatCode && bits == 32;
            if (!isFloat && !(format == PcmCode && bits == 16)) throw new InvalidDataException($"unsupported WAV encoding (format {format}, {bits} bits)");
            sampleCount = dataLength / BytesPerSample;
            Duration = sampleCount / (double)PcmFormat.SampleRate;
        }
        catch
        {
            stream.Dispose();
            throw;
        }
    }

    public double Duration { get; }

    private int BytesPerSample => isFloat ? 4 : 2;

    public float[] Read(double start, double end)
    {
        var first = (long)Math.Clamp(Math.Round(start * PcmFormat.SampleRate), 0, sampleCount);
        var last = (long)Math.Clamp(Math.Round(end * PcmFormat.SampleRate), 0, sampleCount);
        if (last <= first) return [];
        var bytes = new byte[(last - first) * BytesPerSample];
        lock (gate)
        {
            stream.Position = dataOffset + first * BytesPerSample;
            stream.ReadExactly(bytes);
        }
        var samples = new float[last - first];
        if (isFloat)
        {
            Buffer.BlockCopy(bytes, 0, samples, 0, bytes.Length);
        }
        else
        {
            for (var i = 0; i < samples.Length; i++) samples[i] = BitConverter.ToInt16(bytes, i * 2) / 32768f;
        }
        return samples;
    }

    public void Dispose() => stream.Dispose();

    private static string Tag(BinaryReader reader) => Encoding.ASCII.GetString(reader.ReadBytes(4));
}
