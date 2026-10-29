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
        // armv8.2-a+dotprod+fp16. x86_64 is only for the emulator, whose
        // cpuinfo leaves out AVX2, FMA and BMI2 although it runs them.
        if (arch != "aarch64") return emptyList()
        val required = listOf("asimddp", "fphp", "asimdhp")
        val cores = cpuinfo.lineSequence()
            .filter { it.substringBefore(':').trim() == "Features" }
            .map { it.substringAfter(':').trim().split(Regex("\\s+")).toSet() }
            .toList()
        return required.filter { feature -> cores.any { feature !in it } }
    }
}
