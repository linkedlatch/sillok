package io.github.linkedlatch.sillok

import android.net.Uri
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.linkedlatch.sillok.data.ModelDownloader
import io.github.linkedlatch.sillok.data.TranscriptionRunner
import io.github.linkedlatch.sillok.engine.Engine
import io.github.linkedlatch.sillok.engine.Job
import io.github.linkedlatch.sillok.media.AacWriter
import java.io.File
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * How fast the read step is on a long recording like a phone recorder's
 * (AAC, 48 kHz stereo), and where its time goes. Logs the numbers; runs with
 * the end-to-end tests (it needs the real models for a job).
 */
@RunWith(AndroidJUnit4::class)
class ReadSpeedTest {
    private val app = InstrumentationRegistry.getInstrumentation().targetContext.applicationContext as SillokApplication

    @Test
    fun readsTenMinutesOfStereoAac() {
        assumeTrue("end-to-end run only", InstrumentationRegistry.getArguments().getString("speech") != null)
        val transcriber = app.transcriber
        val downloader = ModelDownloader(transcriber.modelsDir)
        runBlocking { downloader.download(downloader.missing()) {} }

        val rate = 48_000
        val seconds = 600
        val recording = File(app.cacheDir, "long.m4a")
        val encodeStart = System.nanoTime()
        AacWriter(recording, rate, bitRate = 128_000, channels = 2).use { writer ->
            val block = ShortArray(rate * 2)
            var frame = 0L
            repeat(seconds) {
                for (i in 0 until rate) {
                    val value = (sin(2 * PI * 440 * frame / rate) * 8000 + (frame * 7919 % 2000) - 1000).toInt().toShort()
                    block[2 * i] = value
                    block[2 * i + 1] = value
                    frame++
                }
                writer.write(block)
            }
            assertTrue(writer.finish())
        }
        val encodeMs = (System.nanoTime() - encodeStart) / 1_000_000

        val copy = File(app.cacheDir, "long-copy.m4a")
        Engine(transcriber.modelsDir, app.cacheDir, null, 1).use { engine ->
            Job(engine, "ko", false, emptyList()).use { job ->
                val runner = TranscriptionRunner(app, transcriber.store, transcriber.modelsDir)
                val timings = runner.read(Uri.fromFile(recording), job, copy) { _, _ -> }
                val samples = job.audioLength
                val summary = "read $seconds s of 48 kHz stereo AAC in ${timings.totalMs} ms " +
                    "(${seconds * 1000 / timings.totalMs.coerceAtLeast(1)}x real time): decode ${timings.decodeMs}, " +
                    "push ${timings.pushMs}, copy ${timings.copyMs}, decoder ${timings.decoder}; writing the test file took $encodeMs ms"
                Log.i("sillok-test", summary)
                // Whether the decoder takes many frames per buffer (Android 15+).
                val multipleFrames = android.media.MediaCodecList(android.media.MediaCodecList.REGULAR_CODECS).codecInfos
                    .filter { !it.isEncoder && it.supportedTypes.any { t -> t.equals("audio/mp4a-latm", true) } }
                    .joinToString { info ->
                        val caps = info.getCapabilitiesForType("audio/mp4a-latm")
                        "${info.name}=${caps.isFeatureSupported(android.media.MediaCodecInfo.CodecCapabilities.FEATURE_MultipleFrames)}"
                    }
                Log.i("sillok-test", "AAC decoders and FEATURE_MultipleFrames: $multipleFrames")
                assertTrue(summary, abs(samples - seconds * 16_000L) < 16_000)
            }
        }
    }
}
