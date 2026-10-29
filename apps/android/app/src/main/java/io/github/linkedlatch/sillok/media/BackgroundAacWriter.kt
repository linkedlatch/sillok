package io.github.linkedlatch.sillok.media

import java.io.File
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/**
 * An [AacWriter] on its own thread, so that encoding the listening copy
 * overlaps decoding the recording instead of adding to it. [write] blocks
 * only when the encoder falls behind by more than a few seconds of audio.
 */
class BackgroundAacWriter(file: File, sampleRate: Int) : AutoCloseable {
    private val queue = ArrayBlockingQueue<ShortArray>(8)
    @Volatile private var failure: Throwable? = null
    @Volatile private var written = false
    private val worker = thread(name = "sillok-copy") {
        try {
            AacWriter(file, sampleRate).use { writer ->
                while (true) {
                    val pcm = queue.take()
                    if (pcm === END) {
                        written = writer.finish()
                        break
                    }
                    writer.write(pcm)
                }
            }
        } catch (e: Throwable) {
            failure = e
        }
    }

    fun write(pcm: ShortArray) {
        while (!queue.offer(pcm, 100, TimeUnit.MILLISECONDS)) rethrow()
        rethrow()
    }

    /** Ends the stream, waits for the encoder and completes the file. Returns false if no audio was written. */
    fun finish(): Boolean {
        while (!queue.offer(END, 100, TimeUnit.MILLISECONDS)) rethrow()
        worker.join()
        rethrow()
        return written
    }

    /** Stops the encoder; the file may be incomplete unless [finish] ran. */
    override fun close() {
        if (!worker.isAlive) return
        queue.clear()
        queue.offer(END)
        worker.join()
    }

    private fun rethrow() {
        failure?.let { throw it }
        check(worker.isAlive || written) { "the listening copy's encoder stopped" }
    }

    private companion object {
        val END = ShortArray(0)
    }
}
