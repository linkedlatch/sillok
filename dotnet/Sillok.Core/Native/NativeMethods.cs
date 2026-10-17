using System.Runtime.InteropServices;

namespace Sillok.Core.Native;

internal static unsafe partial class NativeMethods
{
    private const string Library = "sillok";

    [LibraryImport(Library)]
    internal static partial void sillok_free(IntPtr text);

    [LibraryImport(Library)]
    internal static partial void sillok_buffer_assign(IntPtr buffer, float* samples, nuint count);

    [LibraryImport(Library)]
    internal static partial IntPtr sillok_system_info();

    [LibraryImport(Library)]
    internal static partial int sillok_vad_frame_size();

    [LibraryImport(Library)]
    internal static partial IntPtr sillok_cancel_new();

    [LibraryImport(Library)]
    internal static partial void sillok_cancel_set(IntPtr cancel);

    [LibraryImport(Library)]
    internal static partial void sillok_cancel_free(IntPtr cancel);

    [LibraryImport(Library)]
    internal static partial IntPtr sillok_engine_new();

    [LibraryImport(Library)]
    internal static partial void sillok_engine_free(IntPtr engine);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_engine_load(IntPtr engine, string ggmlPath, string? coreMLEncoderPath, int useGpu);

    [LibraryImport(Library)]
    internal static partial void sillok_engine_unload(IntPtr engine);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_vad_new(string modelPath, out IntPtr error);

    [LibraryImport(Library)]
    internal static partial void sillok_vad_free(IntPtr vad);

    [LibraryImport(Library)]
    internal static partial IntPtr sillok_speech_map_new(IntPtr vad);

    [LibraryImport(Library)]
    internal static partial void sillok_speech_map_free(IntPtr map);

    [LibraryImport(Library)]
    internal static partial IntPtr sillok_speech_map_append(IntPtr map, float* samples, nuint count);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_speech_map_plan(IntPtr map, double duration, string? plannerJson);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_transcribe_step(
        IntPtr engine, string stateJson, string chunksJson, string optionsJson,
        delegate* unmanaged[Cdecl]<IntPtr, double, double, IntPtr, void> read, IntPtr readContext, IntPtr cancel);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_diarizer_new(string modelPath, int useGpu, out IntPtr error);

    [LibraryImport(Library)]
    internal static partial void sillok_diarizer_free(IntPtr diarizer);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_diarize(
        IntPtr diarizer, string segmentsJson, double duration,
        delegate* unmanaged[Cdecl]<IntPtr, double, double, IntPtr, void> read, IntPtr readContext, IntPtr cancel);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr sillok_export(string segmentsJson, string format);
}
