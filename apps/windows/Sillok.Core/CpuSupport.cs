using System.Runtime.InteropServices;
using System.Runtime.Intrinsics.X86;

namespace Sillok;

/// <summary>
/// Whether this CPU has the instructions the library is built for: AVX2 with
/// FMA and BMI2 on x64 (.github/workflows/windows.yml). Without them the
/// library stops the app with an illegal instruction, so the app checks
/// before it touches <see cref="SillokLibrary"/>.
/// </summary>
public static class CpuSupport
{
    /// <summary>The required CPU features this computer lacks; empty when it can run the library.</summary>
    public static IReadOnlyList<string> Missing { get; } = Find();

    private static List<string> Find()
    {
        var missing = new List<string>();
        if (RuntimeInformation.ProcessArchitecture != Architecture.X64) return missing;
        if (!Avx2.IsSupported) missing.Add("AVX2");
        if (!Fma.IsSupported) missing.Add("FMA");
        if (!Bmi2.IsSupported) missing.Add("BMI2");
        return missing;
    }
}
