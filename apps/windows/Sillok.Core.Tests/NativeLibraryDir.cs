using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Sillok.Tests;

internal static class NativeLibraryDir
{
    private static readonly string[] Files = ["sillok.dll", "libsillok.so", "libsillok.dylib"];

    /// <summary>Loads the sillok library from SILLOK_NATIVE_DIR when it is set.</summary>
    [ModuleInitializer]
    internal static void Install()
    {
        var dir = Environment.GetEnvironmentVariable("SILLOK_NATIVE_DIR");
        if (string.IsNullOrEmpty(dir)) return;
        NativeLibrary.SetDllImportResolver(typeof(SillokLibrary).Assembly, (name, assembly, path) =>
        {
            if (name != "sillok") return 0;
            foreach (var file in Files)
            {
                var candidate = Path.Combine(dir, file);
                if (File.Exists(candidate)) return NativeLibrary.Load(candidate);
            }
            return 0;
        });
    }
}
