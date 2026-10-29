package io.github.linkedlatch.sillok

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.github.linkedlatch.sillok.engine.CpuSupport
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** The check that keeps the library off CPUs without its instructions. */
@RunWith(AndroidJUnit4::class)
class CpuSupportTest {
    @Test
    fun thisDeviceRunsTheLibrary() {
        assertEquals(emptyList<String>(), CpuSupport.missing)
    }

    @Test
    fun everyCoreNeedsTheFeatures() {
        val a76 = "fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp"
        val a53 = "fp asimd evtstrm aes pmull sha1 sha2 crc32 cpuid"
        val bigLittle = "processor\t: 0\nFeatures\t: $a53\n\nprocessor\t: 1\nFeatures\t: $a76\n"
        assertEquals(listOf("asimddp", "fphp", "asimdhp"), CpuSupport.missing(bigLittle, "aarch64"))
        assertEquals(emptyList<String>(), CpuSupport.missing("Features\t: $a76\n", "aarch64"))
    }

    @Test
    fun emulatorNeedsAvx2() {
        assertEquals(listOf("avx2", "fma", "f16c", "bmi2"), CpuSupport.missing("flags\t\t: fpu sse4_2 avx\n", "x86_64"))
        assertEquals(emptyList<String>(), CpuSupport.missing("flags\t\t: avx avx2 fma f16c bmi2\n", "x86_64"))
    }

    @Test
    fun unknownFormatsDoNotBlock() {
        assertEquals(emptyList<String>(), CpuSupport.missing("", "aarch64"))
        assertEquals(emptyList<String>(), CpuSupport.missing("Features\t: fp\n", "riscv64"))
    }
}
