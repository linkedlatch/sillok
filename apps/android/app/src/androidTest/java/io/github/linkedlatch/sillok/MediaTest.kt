package io.github.linkedlatch.sillok

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.linkedlatch.sillok.data.JobStore
import io.github.linkedlatch.sillok.engine.Downsampler
import io.github.linkedlatch.sillok.media.AacWriter
import io.github.linkedlatch.sillok.media.AudioDecoder
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The listening copy: downsampling, AAC encoding and decoding it again. */
@RunWith(AndroidJUnit4::class)
class MediaTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    /** [seconds] of a 440 Hz tone as interleaved 16-bit PCM in a direct buffer. */
    private fun tone(rate: Int, channels: Int, seconds: Double): ByteBuffer {
        val frames = (rate * seconds).toInt()
        val buffer = ByteBuffer.allocateDirect(frames * channels * 2).order(ByteOrder.nativeOrder())
        for (i in 0 until frames) {
            val sample = (sin(2 * PI * 440 * i / rate) * 12000).toInt().toShort()
            repeat(channels) { buffer.putShort(sample) }
        }
        return buffer
    }

    @Test
    fun downsamplerKeepsTheDuration() {
        val pcm = tone(44_100, 2, 3.0)
        Downsampler(44_100).use { downsampler ->
            // Feed in blocks as a decoder would.
            var produced = 0
            val block = 4096 * 2 * 2
            var offset = 0
            while (offset < pcm.capacity()) {
                val size = minOf(block, pcm.capacity() - offset)
                produced += downsampler.process(pcm, offset, size, 2, false).size
                offset += size
            }
            produced += downsampler.finish().size
            assertEquals(48_000, produced)
        }
    }

    @Test
    fun copyDecodesToTheSameLength() {
        val file = File(context.cacheDir, "copy.m4a")
        val pcm = tone(48_000, 1, 3.0)
        Downsampler(48_000).use { downsampler ->
            val writer = AacWriter(file, Downsampler.SAMPLE_RATE)
            writer.write(downsampler.process(pcm, 0, pcm.capacity(), 1, false))
            writer.write(downsampler.finish())
            assertTrue(writer.finish())
        }
        assertTrue(file.length() in 4_000..20_000)  // 3 s at 24 kbit/s is about 9 kB

        var frames = 0L
        var rate = 0
        AudioDecoder(context, Uri.fromFile(file)).decode({ false }, {}) { chunk ->
            rate = chunk.sampleRate
            frames += chunk.size / (if (chunk.isFloat) 4 else 2) / chunk.channels
        }
        assertEquals(16_000, rate)
        // AAC adds encoder delay and pads the last frame; both stay well under
        // the half second the transcript tolerates.
        val seconds = frames.toDouble() / rate
        assertTrue("decoded $seconds s", abs(seconds - 3.0) < 0.25)
    }

    @Test
    fun storeListsOnlyCompletedJobs() {
        val root = File(context.cacheDir, "jobs-test").apply { deleteRecursively() }
        val store = JobStore(root)
        val unfinished = store.create()
        val done = store.create()
        val entry = store.complete(done, "{}", "lecture.m4a", 60_000)
        assertEquals(listOf(entry), store.list())
        assertEquals("lecture", entry.title)

        store.removeIncomplete()
        assertTrue(!unfinished.exists())
        store.rename(entry, "Week 1")
        assertEquals("Week 1", store.get(entry.id)?.title)
        store.delete(entry.id)
        assertTrue(store.list().isEmpty())
    }
}
