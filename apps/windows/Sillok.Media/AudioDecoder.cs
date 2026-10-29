using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Media.MediaFoundation;

namespace Sillok.Media;

/// <summary>Decoded samples: <paramref name="channels"/> interleaved floats per frame.</summary>
public delegate void PcmHandler(ReadOnlySpan<float> samples, int sampleRate, int channels);

/// <summary>
/// Decodes the first audio stream of an audio or video file to float PCM
/// with Media Foundation's source reader, which picks the system decoder.
/// </summary>
public static class AudioDecoder
{
    private static readonly uint FirstAudioStream = unchecked((uint)MF_SOURCE_READER_CONSTANTS.MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    private static readonly uint AllStreams = unchecked((uint)MF_SOURCE_READER_CONSTANTS.MF_SOURCE_READER_ALL_STREAMS);
    private static readonly uint MediaSource = unchecked((uint)MF_SOURCE_READER_CONSTANTS.MF_SOURCE_READER_MEDIASOURCE);

    /// <summary>
    /// Calls <paramref name="onPcm"/> for each decoded block, in order;
    /// <paramref name="onProgress"/> receives the fraction decoded. Returns
    /// early once <paramref name="isCancelled"/> turns true.
    /// </summary>
    public static unsafe void Decode(string path, Func<bool> isCancelled, Action<double> onProgress, PcmHandler onPcm)
    {
        MediaFoundation.Start();
        IMFSourceReader reader;
        try
        {
            PInvoke.MFCreateSourceReaderFromURL(path, null, out reader).ThrowOnFailure();
        }
        catch (COMException e)
        {
            throw new UnsupportedMediaException($"cannot open the file (0x{e.HResult:X8})", e);
        }
        try
        {
            IMFMediaType request;
            try
            {
                reader.SetStreamSelection(AllStreams, false);
                reader.SetStreamSelection(FirstAudioStream, true);
                // Float output: the source reader adds the decoder for the stream.
                PInvoke.MFCreateMediaType(out request).ThrowOnFailure();
                request.SetGUID(PInvoke.MF_MT_MAJOR_TYPE, PInvoke.MFMediaType_Audio);
                request.SetGUID(PInvoke.MF_MT_SUBTYPE, PInvoke.MFAudioFormat_Float);
                reader.SetCurrentMediaType(FirstAudioStream, request);
            }
            catch (COMException e)
            {
                throw new UnsupportedMediaException($"no decodable audio stream (0x{e.HResult:X8})", e);
            }
            MediaFoundation.Release(request);
            var (rate, channels) = CurrentFormat(reader);
            var duration = Duration(reader);

            while (!isCancelled())
            {
                reader.ReadSample(FirstAudioStream, 0, out _, out var flags, out var timestamp, out var sample);
                try
                {
                    var streamFlags = (MF_SOURCE_READER_FLAG)flags;
                    if ((streamFlags & MF_SOURCE_READER_FLAG.MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
                    {
                        (rate, channels) = CurrentFormat(reader);
                    }
                    if (sample is not null)
                    {
                        sample.ConvertToContiguousBuffer(out var buffer);
                        try
                        {
                            buffer.Lock(out var data, out _, out var length);
                            try
                            {
                                onPcm(new ReadOnlySpan<float>(data, (int)(length / sizeof(float))), rate, channels);
                            }
                            finally
                            {
                                buffer.Unlock();
                            }
                        }
                        finally
                        {
                            MediaFoundation.Release(buffer);
                        }
                        if (duration > 0) onProgress(Math.Clamp((double)timestamp / duration, 0, 1));
                    }
                    if ((streamFlags & MF_SOURCE_READER_FLAG.MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
                }
                finally
                {
                    MediaFoundation.Release(sample);
                }
            }
        }
        finally
        {
            MediaFoundation.Release(reader);
        }
    }

    private static (int Rate, int Channels) CurrentFormat(IMFSourceReader reader)
    {
        reader.GetCurrentMediaType(FirstAudioStream, out var type);
        try
        {
            type.GetUINT32(PInvoke.MF_MT_AUDIO_SAMPLES_PER_SECOND, out var rate);
            type.GetUINT32(PInvoke.MF_MT_AUDIO_NUM_CHANNELS, out var channels);
            return ((int)rate, (int)channels);
        }
        finally
        {
            MediaFoundation.Release(type);
        }
    }

    /// <summary>The file's duration in 100 ns units, or 0 when unknown.</summary>
    private static long Duration(IMFSourceReader reader)
    {
        try
        {
            reader.GetPresentationAttribute(MediaSource, PInvoke.MF_PD_DURATION, out var value);
            var duration = (long)value.uhVal;
            PInvoke.PropVariantClear(ref value);
            return duration;
        }
        catch (COMException)
        {
            return 0;
        }
    }
}
