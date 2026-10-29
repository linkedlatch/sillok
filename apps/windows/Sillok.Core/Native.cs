using System.Runtime.InteropServices;

namespace Sillok;

/// <summary>
/// The C ABI of include/sillok/sillok.h. Strings are UTF-8; strings the
/// library returns through <c>char**</c> are freed with <see cref="FreeString"/>.
/// </summary>
internal static unsafe partial class Native
{
    private const string Library = "sillok";  // sillok.dll, libsillok.so, libsillok.dylib

    public const int Ok = 0;
    public const int InvalidArgument = 1;
    public const int ModelError = 2;
    public const int Cancelled = 3;

    [LibraryImport(Library, EntryPoint = "sillok_version")]
    public static partial nint Version();

    [LibraryImport(Library, EntryPoint = "sillok_last_error")]
    public static partial nint LastError();

    [LibraryImport(Library, EntryPoint = "sillok_free_string")]
    public static partial void FreeString(nint text);

    [LibraryImport(Library, EntryPoint = "sillok_model_catalog")]
    public static partial int ModelCatalog(out nint json);

    [LibraryImport(Library, EntryPoint = "sillok_model_check", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int ModelCheck(string modelsDir, int verifyHash, out nint json);

    [LibraryImport(Library, EntryPoint = "sillok_devices")]
    public static partial int Devices(out nint json);

    [LibraryImport(Library, EntryPoint = "sillok_engine_create", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int EngineCreate(string config, out nint engine);

    [LibraryImport(Library, EntryPoint = "sillok_engine_destroy")]
    public static partial void EngineDestroy(nint engine);

    [LibraryImport(Library, EntryPoint = "sillok_job_create", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int JobCreate(nint engine, string options, out nint job);

    [LibraryImport(Library, EntryPoint = "sillok_job_push_audio")]
    public static partial int JobPushAudio(nint job, float* samples, nuint frames, int sampleRate, int channels);

    [LibraryImport(Library, EntryPoint = "sillok_job_end_audio")]
    public static partial int JobEndAudio(nint job);

    [LibraryImport(Library, EntryPoint = "sillok_job_audio_length")]
    public static partial int JobAudioLength(nint job, out ulong samples);

    [LibraryImport(Library, EntryPoint = "sillok_job_read_audio")]
    public static partial int JobReadAudio(nint job, ulong start, float* samples, nuint count, out nuint read);

    [LibraryImport(Library, EntryPoint = "sillok_job_run")]
    public static partial int JobRun(
        nint job, delegate* unmanaged[Cdecl]<nint, int, double, void> progress, nint userData, out nint json);

    [LibraryImport(Library, EntryPoint = "sillok_job_cancel")]
    public static partial void JobCancel(nint job);

    [LibraryImport(Library, EntryPoint = "sillok_job_destroy")]
    public static partial void JobDestroy(nint job);

    [LibraryImport(Library, EntryPoint = "sillok_export", StringMarshalling = StringMarshalling.Utf8)]
    public static partial int Export(string transcript, string format, string? options, out nint text);

    /// <summary>Throws for a failed status with the library's message.</summary>
    public static void Check(int status)
    {
        if (status != Ok) throw new SillokException(status, Marshal.PtrToStringUTF8(LastError()) ?? "");
    }

    /// <summary>Returns a string the library allocated and frees it.</summary>
    public static string Take(nint text)
    {
        try
        {
            return Marshal.PtrToStringUTF8(text) ?? "";
        }
        finally
        {
            FreeString(text);
        }
    }
}

/// <summary>A failed library call; <see cref="Status"/> is a sillok_status value.</summary>
public sealed class SillokException(int status, string message) : Exception(message)
{
    public int Status { get; } = status;
    public bool IsModelError => Status == Native.ModelError;
    public bool IsCancelled => Status == Native.Cancelled;
}
