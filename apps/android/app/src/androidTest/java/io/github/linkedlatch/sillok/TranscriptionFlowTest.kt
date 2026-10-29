package io.github.linkedlatch.sillok

import android.Manifest
import android.net.Uri
import android.os.ParcelFileDescriptor
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.linkedlatch.sillok.data.JobSettings
import io.github.linkedlatch.sillok.data.ModelDownloader
import io.github.linkedlatch.sillok.engine.Transcript
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The whole flow with the real models, as a user starts it: the app in front,
 * the job in the foreground service, the result in the history. Needs a
 * speech recording on the device, passed as the instrumentation argument
 * "speech" (CI pushes a FLEURS Korean utterance); downloads the models.
 */
@RunWith(AndroidJUnit4::class)
class TranscriptionFlowTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app = instrumentation.targetContext.applicationContext as SillokApplication

    @Test
    fun transcribesInTheServiceWithTheRealModels() {
        val speech = InstrumentationRegistry.getArguments().getString("speech")
        assumeTrue("pass -e speech <path on the device>", speech != null)
        val transcriber = app.transcriber

        val downloader = ModelDownloader(transcriber.modelsDir)
        runBlocking { downloader.download(downloader.missing()) {} }
        // Only the shell may read /data/local/tmp.
        val file = File(app.cacheDir, "speech.wav")
        ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand("cat $speech"))
            .use { input -> file.outputStream().use { input.copyTo(it) } }
        assertTrue(file.length() > 100_000)
        instrumentation.uiAutomation.grantRuntimePermission(app.packageName, Manifest.permission.POST_NOTIFICATIONS)

        ActivityScenario.launch(MainActivity::class.java).use {
            val before = transcriber.store.list().map { it.id }.toSet()
            instrumentation.runOnMainSync {
                transcriber.start(Uri.fromFile(file), "speech.wav", JobSettings(language = "ko", diarize = true))
            }
            // The screen takes the outcome as soon as it comes, so watch the
            // state end instead.
            val deadline = System.currentTimeMillis() + 30 * 60_000L
            val steps = mutableSetOf<String>()
            while (true) {
                val state = transcriber.state.value ?: break
                steps += state.step.name
                assertTrue("still running after 30 minutes", System.currentTimeMillis() < deadline)
                Thread.sleep(200)
            }

            val added = transcriber.store.list().filter { it.id !in before }
            assertEquals("a new job in the history (steps seen: $steps)", 1, added.size)
            val entry = added.single()
            val transcript = Transcript(transcriber.store.transcriptFile(entry.id).readText())
            val text = transcript.segments.joinToString(" ") { it.text }
            assertTrue("Korean text: $text", text.any { it in '가'..'힣' })
            assertTrue(transcriber.store.audioFile(entry.id).length() > 1_000)
            val timings = entry.stats!!.timingsMs.keys
            assertTrue(timings.toString(), timings.containsAll(listOf("read", "analyze", "transcribe")))
            android.util.Log.i("sillok-test", "steps $steps, timings ${entry.stats.timingsMs}, text: $text")
        }
    }
}
