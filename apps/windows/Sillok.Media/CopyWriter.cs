using System.Collections.Concurrent;

namespace Sillok.Media;

/// <summary>
/// Writes the listening copy on its own thread, so that encoding overlaps
/// decoding: takes the job's 16 kHz samples, raises them to 48 kHz for the
/// AAC encoder and writes them. <see cref="Write"/> blocks only when the
/// encoder falls behind by several seconds of audio.
/// </summary>
internal sealed class CopyWriter : IDisposable
{
    private readonly BlockingCollection<float[]> _queue = new(boundedCapacity: 8);
    private readonly Thread _worker;
    private Exception? _failure;
    private bool _written;

    public CopyWriter(string path)
    {
        _worker = new Thread(() => Encode(path)) { Name = "sillok-copy", IsBackground = true };
        _worker.Start();
    }

    public void Write(float[] samples)
    {
        Rethrow();
        try
        {
            _queue.Add(samples);
        }
        catch (InvalidOperationException)
        {
            Rethrow();  // the worker stopped taking samples
            throw;
        }
    }

    /// <summary>Ends the stream, waits for the encoder and completes the file. Returns false if no audio was written.</summary>
    public bool Finish()
    {
        _queue.CompleteAdding();
        _worker.Join();
        Rethrow();
        return _written;
    }

    public void Dispose()
    {
        if (!_queue.IsAddingCompleted) _queue.CompleteAdding();
        _worker.Join();
        _queue.Dispose();
    }

    private void Encode(string path)
    {
        try
        {
            var upsampler = new Upsampler();
            using var writer = new AacFileWriter(path);
            foreach (var samples in _queue.GetConsumingEnumerable()) writer.Write(ToPcm16(upsampler.Process(samples)));
            writer.Write(ToPcm16(upsampler.Finish()));
            _written = writer.Finish();
        }
        catch (Exception e)
        {
            _failure = e;
            // Stop the producer from waiting on a queue nobody empties.
            _queue.CompleteAdding();
        }
    }

    private void Rethrow()
    {
        if (_failure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Throw(_failure);
    }

    private static short[] ToPcm16(float[] samples)
    {
        var pcm = new short[samples.Length];
        for (var i = 0; i < samples.Length; i++) pcm[i] = (short)Math.Clamp(MathF.Round(samples[i] * 32768f), -32768f, 32767f);
        return pcm;
    }
}
