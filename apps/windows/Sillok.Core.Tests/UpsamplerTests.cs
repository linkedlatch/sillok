namespace Sillok.Tests;

public class UpsamplerTests
{
    private static float[] Run(float[] input, int block)
    {
        var upsampler = new Upsampler();
        var output = new List<float>();
        for (var i = 0; i < input.Length; i += block) output.AddRange(upsampler.Process(input.AsSpan(i, Math.Min(block, input.Length - i))));
        output.AddRange(upsampler.Finish());
        return [.. output];
    }

    [Theory]
    [InlineData(1)]
    [InlineData(5)]
    [InlineData(16000)]
    public void TriplesEveryLengthExactly(int length)
    {
        Assert.Equal(length * 3, Run(new float[length], 997).Length);
    }

    [Fact]
    public void KeepsAToneAndItsTiming()
    {
        // 1 kHz at 16 kHz, compared with 1 kHz computed at 48 kHz.
        var input = Enumerable.Range(0, 16000).Select(i => (float)Math.Sin(2 * Math.PI * 1000 * i / 16000)).ToArray();
        var output = Run(input, 333);
        var error = 0.0;
        for (var i = 300; i < output.Length - 300; i++) error = Math.Max(error, Math.Abs(output[i] - Math.Sin(2 * Math.PI * 1000 * i / 48000)));
        Assert.True(error < 0.01, $"largest error {error}");
    }

    [Fact]
    public void BlockSizeDoesNotChangeTheResult()
    {
        var input = Enumerable.Range(0, 5000).Select(i => (float)Math.Sin(i * 0.37) * 0.5f).ToArray();
        Assert.Equal(Run(input, 5000), Run(input, 7));
    }
}
