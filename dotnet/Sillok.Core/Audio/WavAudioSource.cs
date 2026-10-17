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
            if (Tag(reader) != "RIFF") throw new InvalidDataException("WAV 파일이 아닙니다");
            reader.ReadUInt32();
            if (Tag(reader) != "WAVE") throw new InvalidDataException("WAV 파일이 아닙니다");

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

            if (dataLength < 0) throw new InvalidDataException("WAV 데이터가 없습니다");
            if (rate != PcmFormat.SampleRate || channels != 1) throw new InvalidDataException($"16kHz 모노 WAV가 아닙니다 ({rate}Hz, {channels}채널)");
            isFloat = format == FloatCode && bits == 32;
            if (!isFloat && !(format == PcmCode && bits == 16)) throw new InvalidDataException($"지원하지 않는 WAV 형식입니다 (형식 {format}, {bits}비트)");
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
