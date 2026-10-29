package io.github.linkedlatch.sillok.engine

import java.io.File

/**
 * Whether this CPU has the instructions the library is built for
 * (app/src/main/cpp/CMakeLists.txt). Without them the library stops the app
 * with an illegal instruction, so the app checks before it touches [Sillok].
 */
object CpuSupport {
    /** The required CPU features this device lacks; empty when it can run the library. */
    val missing: List<String> by lazy {
        val cpuinfo = runCatching { File("/proc/cpuinfo").readText() }.getOrNull()
        // Without the file there is nothing to go by: let the app try.
        if (cpuinfo == null) emptyList() else missing(cpuinfo, System.getProperty("os.arch").orEmpty())
    }

    /** The features of [arch] that some core in [cpuinfo] lacks. */
    internal fun missing(cpuinfo: String, arch: String): List<String> {
        val (key, required) = when (arch) {
            // armv8.2-a+dotprod+fp16
            "aarch64" -> "Features" to listOf("asimddp", "fphp", "asimdhp")
            // The emulator build: AVX2 with FMA, F16C and BMI2.
            "x86_64" -> "flags" to listOf("avx2", "fma", "f16c", "bmi2")
            else -> return emptyList()
        }
        val cores = cpuinfo.lineSequence()
            .filter { it.substringBefore(':').trim() == key }
            .map { it.substringAfter(':').trim().split(Regex("\\s+")).toSet() }
            .toList()
        return required.filter { feature -> cores.any { feature !in it } }
    }
}
