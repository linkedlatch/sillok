using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace Sillok.Core.Native;

public sealed class NativeException(string message) : Exception(message);

internal static class NativeCall
{
    private static readonly SemaphoreSlim Gate = new(1, 1);

    internal static async Task<T> Run<T>(Func<T> work)
    {
        await Gate.WaitAsync().ConfigureAwait(false);
        try
        {
            return await Task.Factory.StartNew(work, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default)
                .ConfigureAwait(false);
        }
        finally
        {
            Gate.Release();
        }
    }

    internal static async Task<T> WithCancellation<T>(CancellationToken cancellation, Func<IntPtr, T> work)
    {
        cancellation.ThrowIfCancellationRequested();
        var cancel = NativeMethods.sillok_cancel_new();
        try
        {
            using var registration = cancellation.Register(() => NativeMethods.sillok_cancel_set(cancel));
            try
            {
                return await Run(() => work(cancel)).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
            {
                throw new OperationCanceledException(cancellation);
            }
        }
        finally
        {
            NativeMethods.sillok_cancel_free(cancel);
        }
    }

    internal static T Decode<T>(IntPtr result)
    {
        var json = TakeString(result) ?? throw new NativeException("no response from the native core");
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.GetProperty("ok").GetBoolean())
        {
            return root.GetProperty("value").Deserialize<T>(SillokJson.Options) ?? throw new NativeException("empty response from the native core");
        }
        if (root.TryGetProperty("cancelled", out var cancelled) && cancelled.GetBoolean()) throw new OperationCanceledException();
        throw new NativeException(root.TryGetProperty("error", out var error) ? error.GetString() ?? "unknown native error" : "unknown native error");
    }

    internal static void Check(IntPtr result) => Decode<bool>(result);

    internal static string? TakeString(IntPtr text)
    {
        if (text == IntPtr.Zero) return null;
        try
        {
            return Marshal.PtrToStringUTF8(text);
        }
        finally
        {
            NativeMethods.sillok_free(text);
        }
    }
}

internal sealed class AudioContext : IDisposable
{
    private GCHandle handle;

    internal AudioContext(IAudioSource source) => handle = GCHandle.Alloc(source);

    internal IntPtr Pointer => GCHandle.ToIntPtr(handle);

    public void Dispose() => handle.Free();

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe void Read(IntPtr context, double start, double end, IntPtr output)
    {
        float[] samples;
        try
        {
            samples = ((IAudioSource)GCHandle.FromIntPtr(context).Target!).Read(start, end);
        }
        catch
        {
            samples = [];
        }
        fixed (float* pointer = samples) NativeMethods.sillok_buffer_assign(output, pointer, (nuint)samples.Length);
    }
}
