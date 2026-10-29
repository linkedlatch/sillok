namespace Sillok;

/// <summary>
/// Streams 16 kHz mono up to 48 kHz (three times), for platform encoders
/// that do not take 16 kHz, such as Windows' AAC encoder. A Kaiser-windowed
/// sinc removes the images above 8 kHz; output sample 3n is input sample n.
/// </summary>
public sealed class Upsampler
{
    public const int Factor = 3;
    private const int HalfTaps = 16;  // input samples on each side

    // Phase p (0..2) of output 3n+p sits at input position n + p/3; its taps
    // are inputs n-HalfTaps+1 .. n+HalfTaps.
    private const int Taps = 2 * HalfTaps;
    private static readonly float[][] Kernel = BuildKernel();
    // The last Taps inputs, stored twice so that they are always contiguous.
    private readonly float[] _window = new float[2 * Taps];
    private int _position;
    private long _inputs;  // samples given to Process
    private long _pushed;  // samples entered into the window, with the zeros of Finish

    /// <summary>Converts the next block: three samples per input sample, the last few held back until <see cref="Finish"/>.</summary>
    public float[] Process(ReadOnlySpan<float> input)
    {
        var output = new List<float>(input.Length * Factor);
        _inputs += input.Length;
        foreach (var sample in input) Push(sample, output);
        return [.. output];
    }

    /// <summary>Flushes the held-back samples.</summary>
    public float[] Finish()
    {
        var output = new List<float>(Taps * Factor);
        while (_pushed < _inputs + HalfTaps) Push(0f, output);
        return [.. output];
    }

    private void Push(float sample, List<float> output)
    {
        _window[_position] = sample;
        _window[_position + Taps] = sample;
        _position = (_position + 1) % Taps;
        _pushed++;
        // The newest sample is input n + HalfTaps, with zeros before the start.
        var n = _pushed - HalfTaps - 1;
        if (n < 0 || n >= _inputs) return;
        var history = _window.AsSpan(_position, Taps);  // oldest first
        for (var phase = 0; phase < Factor; phase++)
        {
            var taps = Kernel[phase];
            float sum = 0;
            for (var j = 0; j < Taps; j++) sum += taps[j] * history[j];
            output.Add(sum);
        }
    }

    private static float[][] BuildKernel()
    {
        const double beta = 8.0;
        const double cutoff = 0.5 * 0.94;  // of the input rate: 7.5 kHz
        var kernel = new float[Factor][];
        for (var phase = 0; phase < Factor; phase++)
        {
            var taps = new double[2 * HalfTaps];
            double sum = 0;
            for (var j = 0; j < taps.Length; j++)
            {
                // Distance from output position n + phase/3 to input n - HalfTaps + 1 + j.
                var t = j - (HalfTaps - 1) - (double)phase / Factor;
                var x = t / HalfTaps;
                var window = Math.Abs(x) >= 1 ? 0 : BesselI0(beta * Math.Sqrt(1 - x * x)) / BesselI0(beta);
                var arg = 2 * cutoff * t;
                var sinc = arg == 0 ? 1 : Math.Sin(Math.PI * arg) / (Math.PI * arg);
                taps[j] = sinc * window;
                sum += taps[j];
            }
            kernel[phase] = taps.Select(v => (float)(v / sum)).ToArray();  // unit gain at DC
        }
        return kernel;
    }

    private static double BesselI0(double x)
    {
        double sum = 1, term = 1, half = x / 2;
        for (var k = 1; k < 64 && term >= sum * 1e-15; k++)
        {
            term *= half / k * (half / k);
            sum += term;
        }
        return sum;
    }
}
