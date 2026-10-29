package io.github.linkedlatch.sillok

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.linkedlatch.sillok.data.JobStore
import io.github.linkedlatch.sillok.engine.Job
import io.github.linkedlatch.sillok.media.AacWriter
import io.github.linkedlatch.sillok.media.AudioDecoder
import java.io.File
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The listening copy: AAC encoding and decoding it again; the job store. */
@RunWith(AndroidJUnit4::class)
class MediaTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    /** [seconds] of a 440 Hz tone as 16-bit PCM. */
    private fun tone(rate: Int, seconds: Double) =
        ShortArray((rate * seconds).toInt()) { (sin(2 * PI * 440 * it / rate) * 12000).toInt().toShort() }

    @Test
    fun copyDecodesToTheSameLength() {
        val file = File(context.cacheDir, "copy.m4a")
        val writer = AacWriter(file, Job.SAMPLE_RATE)
        // In blocks of a second, as the app copies the job's recording.
        tone(Job.SAMPLE_RATE, 3.0).asList().chunked(Job.SAMPLE_RATE).forEach { writer.write(it.toShortArray()) }
        assertTrue(writer.finish())
        assertTrue(file.length() in 4_000..20_000)  // 3 s at 24 kbit/s is about 9 kB

        var frames = 0L
        var rate = 0
        AudioDecoder(context, Uri.fromFile(file)).decode({ false }, {}) { chunk ->
            rate = chunk.sampleRate
            frames += chunk.size / (if (chunk.isFloat) 4 else 2) / chunk.channels
        }
        assertEquals(Job.SAMPLE_RATE, rate)
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
