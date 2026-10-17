namespace Sillok.Core.Native;

public sealed class NativeEngine : IDisposable
{
    private readonly object gate = new();
    private ModelFiles? loaded;

    public NativeEngine() => Pointer = NativeMethods.sillok_engine_new();

    internal IntPtr Pointer { get; }

    public static string SystemInfo => NativeCall.TakeString(NativeMethods.sillok_system_info()) ?? "";

    public ModelFiles? LoadedFiles
    {
        get
        {
            lock (gate) return loaded;
        }
    }

    public async Task LoadAsync(ModelFiles files, bool useGpu)
    {
        await NativeCall.Run(() =>
        {
            NativeCall.Check(NativeMethods.sillok_engine_load(Pointer, files.Ggml, files.CoreMLEncoder, useGpu ? 1 : 0));
            return true;
        });
        lock (gate) loaded = files;
    }

    public async Task UnloadAsync()
    {
        await NativeCall.Run(() =>
        {
            NativeMethods.sillok_engine_unload(Pointer);
            return true;
        });
        lock (gate) loaded = null;
    }

    public void Dispose() => NativeMethods.sillok_engine_free(Pointer);
}
