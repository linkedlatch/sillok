package io.github.linkedlatch.sillok.data

import android.content.Context
import android.net.Uri
import io.github.linkedlatch.sillok.engine.Downsampler
import io.github.linkedlatch.sillok.engine.Engine
import io.github.linkedlatch.sillok.engine.Job
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.engine.Stage
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.media.AacWriter
import io.github.linkedlatch.sillok.media.AudioDecoder
import io.github.linkedlatch.sillok.media.UnsupportedMediaException
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** The steps of a transcription as the app shows them: reading the file, then the library's stages. */
enum class Step { READ, ANALYZE, TRANSCRIBE, DIARIZE }

/**
 * Runs one transcription: decodes the file, pushes its audio to the library
 * while writing the listening copy, runs the library and stores the result
 * in a new job folder. Single use; [cancel] may be called from any thread.
 */
class TranscriptionRunner(
    private val context: Context,
    private val store: JobStore,
    private val modelsDir: File,
) {
    private val cancelled = AtomicBoolean(false)
    @Volatile private var job: Job? = null

    fun cancel() {
        cancelled.set(true)
        job?.cancel()
    }

    /** Blocks until done. Returns the stored job, or null if cancelled. */
    fun run(uri: Uri, source: String, settings: JobSettings, onProgress: (Step, Double) -> Unit): JobEntry? {
        val dir = store.create()
        var entry: JobEntry? = null
        try {
            Engine(modelsDir, context.cacheDir, settings.gpuIndex, threads()).use { engine ->
                Job(engine, settings.language, settings.diarize, settings.terms).use { job ->
                    this.job = job
                    if (cancelled.get()) return null
                    decode(uri, job, store.audioFile(dir.name), onProgress)
                    if (cancelled.get()) return null
                    val transcript = try {
                        job.run { stage, fraction -> onProgress(stage.step, fraction) }
                    } catch (e: SillokException) {
                        if (e.status == SillokException.CANCELLED) return null
                        throw e
                    } finally {
                        this.job = null
                    }
                    entry = store.complete(dir, transcript, source, Transcript(transcript).durationMs)
                }
            }
        } finally {
            if (entry == null) dir.deleteRecursively()
        }
        return entry
    }

    private fun decode(uri: Uri, job: Job, copy: File, onProgress: (Step, Double) -> Unit) {
        var format: Pair<Int, Int>? = null
        var downsampler: Downsampler? = null
        val writer = AacWriter(copy, Downsampler.SAMPLE_RATE)
        try {
            AudioDecoder(context, uri).decode(cancelled::get, { onProgress(Step.READ, it) }) { pcm ->
                val chunkFormat = pcm.sampleRate to pcm.channels
                if (format == null) {
                    format = chunkFormat
                    downsampler = Downsampler(pcm.sampleRate)
                } else if (format != chunkFormat) {
                    throw UnsupportedMediaException("audio format changes within the file")
                }
                job.pushAudio(pcm.buffer, pcm.offset, pcm.size, pcm.sampleRate, pcm.channels, pcm.isFloat)
                writer.write(downsampler!!.process(pcm.buffer, pcm.offset, pcm.size, pcm.channels, pcm.isFloat))
            }
            if (cancelled.get()) return
            val last = downsampler ?: throw UnsupportedMediaException("the file has no audio")
            writer.write(last.finish())
            writer.finish()
        } finally {
            writer.close()
            downsampler?.close()
        }
    }

    private val Stage.step
        get() = when (this) {
            Stage.ANALYZE -> Step.ANALYZE
            Stage.TRANSCRIBE -> Step.TRANSCRIBE
            Stage.DIARIZE -> Step.DIARIZE
        }

    companion object {
        /**
         * Threads for the CPU: the cores outside the slowest cluster. ggml
         * splits work evenly, so efficiency cores would hold the others back
         * (a Galaxy S22 has 1 + 3 fast and 4 efficiency cores).
         */
        fun threads(): Int {
            val cores = Runtime.getRuntime().availableProcessors()
            val speeds = (0 until cores).mapNotNull {
                runCatching {
                    File("/sys/devices/system/cpu/cpu$it/cpufreq/cpuinfo_max_freq").readText().trim().toLong()
                }.getOrNull()
            }
            if (speeds.size != cores) return cores.coerceAtMost(8)
            val fast = speeds.count { it > speeds.min() }
            return (if (fast == 0) cores else fast).coerceAtMost(8)
        }
    }
}
