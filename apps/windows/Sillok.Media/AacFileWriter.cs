using Windows.Win32;
using Windows.Win32.Media.MediaFoundation;

namespace Sillok.Media;

/// <summary>
/// Writes 16-bit mono PCM at 48 kHz as AAC into an .m4a file with Media
/// Foundation's sink writer. Windows' AAC encoder takes 44.1 or 48 kHz and
/// 96 to 192 kbit/s only; the app raises the job's 16 kHz with
/// <see cref="Upsampler"/>.
/// </summary>
public sealed class AacFileWriter : IDisposable
{
    public const int SampleRate = 48_000;
    private const int BytesPerSecond = 12_000;  // 96 kbit/s, the encoder's lowest
    private IMFSinkWriter? _writer;
    private readonly uint _stream;
    private long _frames;

    public AacFileWriter(string path)
    {
        MediaFoundation.Start();
        PInvoke.MFCreateAttributes(out var attributes, 1).ThrowOnFailure();
        attributes.SetGUID(PInvoke.MF_TRANSCODE_CONTAINERTYPE, PInvoke.MFTranscodeContainerType_MPEG4);
        PInvoke.MFCreateSinkWriterFromURL(path, null, attributes, out var writer).ThrowOnFailure();
        MediaFoundation.Release(attributes);
        _writer = writer;

        var output = AudioType(PInvoke.MFAudioFormat_AAC);
        output.SetUINT32(PInvoke.MF_MT_AUDIO_AVG_BYTES_PER_SECOND, BytesPerSecond);
        writer.AddStream(output, out _stream);
        MediaFoundation.Release(output);

        var input = AudioType(PInvoke.MFAudioFormat_PCM);
        input.SetUINT32(PInvoke.MF_MT_AUDIO_BLOCK_ALIGNMENT, 2);
        input.SetUINT32(PInvoke.MF_MT_AUDIO_AVG_BYTES_PER_SECOND, SampleRate * 2);
        writer.SetInputMediaType(_stream, input, null);
        MediaFoundation.Release(input);
        writer.BeginWriting();
    }

    public unsafe void Write(ReadOnlySpan<short> pcm)
    {
        if (pcm.IsEmpty) return;
        var writer = _writer ?? throw new ObjectDisposedException(nameof(AacFileWriter));
        var bytes = (uint)(pcm.Length * sizeof(short));
        PInvoke.MFCreateMemoryBuffer(bytes, out var buffer).ThrowOnFailure();
        IMFSample? sample = null;
        try
        {
            buffer.Lock(out var data, out _, out _);
            pcm.CopyTo(new Span<short>(data, pcm.Length));
            buffer.Unlock();
            buffer.SetCurrentLength(bytes);
            PInvoke.MFCreateSample(out sample).ThrowOnFailure();
            sample.AddBuffer(buffer);
            sample.SetSampleTime(_frames * 10_000_000 / SampleRate);
            sample.SetSampleDuration((long)pcm.Length * 10_000_000 / SampleRate);
            writer.WriteSample(_stream, sample);
            _frames += pcm.Length;
        }
        finally
        {
            MediaFoundation.Release(sample);
            MediaFoundation.Release(buffer);
        }
    }

    /// <summary>Completes the file. Returns false if no audio was written.</summary>
    public bool Finish()
    {
        var writer = _writer ?? throw new ObjectDisposedException(nameof(AacFileWriter));
        if (_frames > 0) writer.Finalize();
        Dispose();
        return _frames > 0;
    }

    public void Dispose()
    {
        MediaFoundation.Release(_writer);
        _writer = null;
    }

    private static IMFMediaType AudioType(Guid subtype)
    {
        PInvoke.MFCreateMediaType(out var type).ThrowOnFailure();
        type.SetGUID(PInvoke.MF_MT_MAJOR_TYPE, PInvoke.MFMediaType_Audio);
        type.SetGUID(PInvoke.MF_MT_SUBTYPE, subtype);
        type.SetUINT32(PInvoke.MF_MT_AUDIO_SAMPLES_PER_SECOND, SampleRate);
        type.SetUINT32(PInvoke.MF_MT_AUDIO_NUM_CHANNELS, 1);
        type.SetUINT32(PInvoke.MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        return type;
    }
}
