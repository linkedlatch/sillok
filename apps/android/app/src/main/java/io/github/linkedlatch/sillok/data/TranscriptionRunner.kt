package io.github.linkedlatch.sillok.data

import android.content.Context
import android.net.Uri
import android.os.SystemClock
import io.github.linkedlatch.sillok.engine.Engine
import io.github.linkedlatch.sillok.engine.Job
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.engine.Stage
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.media.BackgroundAacWriter
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
    fun run(
        uri: Uri,
        source: String,
        settings: JobSettings,
        onRead: (ReadTimings) -> Unit = {},
        onSegments: (List<Transcript.Segment>) -> Unit = {},
        onProgress: (Step, Double) -> Unit,
    ): JobEntry? {
        val dir = store.create()
        var entry: JobEntry? = null
        // Wall time of each step, kept with the job to compare devices.
        val timings = linkedMapOf<Step, Long>()
        var current: Step? = null
        var since = 0L
        fun enter(step: Step?) {
            val now = SystemClock.elapsedRealtime()
            current?.let { timings[it] = (timings[it] ?: 0L) + now - since }
            current = step
            since = now
        }
        val report = { step: Step, fraction: Double ->
            if (step != current) enter(step)
            onProgress(step, fraction)
        }
        val threads = threads()
        try {
            Engine(modelsDir, context.cacheDir, settings.gpuIndex, threads, settings.transcriptionModel).use { engine ->
                Job(engine, settings.language, settings.diarize, settings.terms).use { job ->
                    this.job = job
                    if (cancelled.get()) return null
                    report(Step.READ, 0.0)
                    val read = read(uri, job, store.audioFile(dir.name), report)
                    if (cancelled.get()) return null
                    onRead(read)
                    val transcript = try {
                        job.run(onSegments) { stage, fraction -> report(stage.step, fraction) }
                    } catch (e: SillokException) {
                        if (e.status == SillokException.CANCELLED) return null
                        throw e
                    } finally {
                        this.job = null
                    }
                    enter(null)
                    val stats = JobStats(
                        timings.mapKeys { it.key.name.lowercase() } +
                            mapOf("read_decode" to read.decodeMs, "read_push" to read.pushMs, "read_copy" to read.copyMs),
                        threads,
                        read.decoder,
                    )
                    entry = store.complete(dir, transcript, source, Transcript(transcript).durationMs, stats)
                }
            }
        } finally {
            if (entry == null) dir.deleteRecursively()
        }
        return entry
    }

    /**
     * Where the time of the read step goes, in milliseconds: [copyMs] is how
     * long the listening copy held decoding up. [decoder] is the codec used.
     */
    data class ReadTimings(val totalMs: Long, val pushMs: Long, val copyMs: Long, val decoder: String?) {
        /** Extracting and decoding: what is left. */
        val decodeMs get() = totalMs - pushMs - copyMs
    }

    /**
     * Decodes the file into [job] and writes the listening copy from the
     * job's own 16 kHz recording, read back as it grows: the copy then holds
     * exactly what the transcript's times refer to.
     */
    internal fun read(uri: Uri, job: Job, copy: File, onProgress: (Step, Double) -> Unit): ReadTimings {
        val start = System.nanoTime()
        var pushNs = 0L
        var copyNs = 0L
        var format: Pair<Int, Int>? = null
        var copied = 0L
        val decoder = AudioDecoder(context, uri)
        BackgroundAacWriter(copy, Job.SAMPLE_RATE).use { writer ->
            // Copies once at least [COPY_BLOCK] samples are new, or everything when
            // [all]; small writes would cost a codec round trip each.
            fun copyNewAudio(all: Boolean = false) {
                val length = job.audioLength
                if (!all && length - copied < COPY_BLOCK) return
                val begin = System.nanoTime()
                while (copied < length) {
                    val pcm = job.readAudio(copied, minOf(length - copied, COPY_BLOCK).toInt())
                    writer.write(pcm)
                    copied += pcm.size
                }
                copyNs += System.nanoTime() - begin
            }
            decoder.decode(cancelled::get, { onProgress(Step.READ, it) }) { pcm ->
                val chunkFormat = pcm.sampleRate to pcm.channels
                if (format == null) {
                    format = chunkFormat
                } else if (format != chunkFormat) {
                    throw UnsupportedMediaException("audio format changes within the file")
                }
                val begin = System.nanoTime()
                job.pushAudio(pcm.buffer, pcm.offset, pcm.size, pcm.sampleRate, pcm.channels, pcm.isFloat)
                pushNs += System.nanoTime() - begin
                copyNewAudio()
            }
            if (!cancelled.get()) {
                if (format == null) throw UnsupportedMediaException("the file has no audio")
                job.endAudio()
                copyNewAudio(all = true)
                val begin = System.nanoTime()
                writer.finish()
                copyNs += System.nanoTime() - begin
            }
        }
        return ReadTimings((System.nanoTime() - start) / 1_000_000, pushNs / 1_000_000, copyNs / 1_000_000, decoder.codecName)
    }

    private val Stage.step
        get() = when (this) {
            Stage.ANALYZE -> Step.ANALYZE
            Stage.TRANSCRIBE -> Step.TRANSCRIBE
            Stage.DIARIZE -> Step.DIARIZE
        }

    companion object {
        private const val COPY_BLOCK = 16_000L  // samples per read, 1 s

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
