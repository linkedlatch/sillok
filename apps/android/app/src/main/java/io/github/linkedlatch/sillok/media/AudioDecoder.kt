package io.github.linkedlatch.sillok.media

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import java.io.IOException
import java.nio.ByteBuffer
import java.util.ArrayDeque
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Decoded PCM in [buffer] at [offset], [size] bytes; 16-bit integers unless [isFloat]. */
class PcmChunk(
    val buffer: ByteBuffer,
    val offset: Int,
    val size: Int,
    val sampleRate: Int,
    val channels: Int,
    val isFloat: Boolean,
)

class UnsupportedMediaException(message: String) : IOException(message)

/**
 * Decodes the first audio track of an audio or video file with the platform
 * decoders (MediaExtractor + MediaCodec), in the codec's asynchronous mode.
 *
 * Every buffer is a round trip to the codec's process, and an AAC frame is
 * 23 ms: one frame per buffer made decoding an hour take minutes on a phone.
 * Codecs that take many frames per buffer (Android 15+, among them the
 * platform's AAC decoder) get about a second of frames each, which they
 * accept in asynchronous mode only.
 */
class AudioDecoder(private val context: Context, private val uri: Uri) {
    /** Name of the codec the last [decode] used, for the run details; "batched" when it took many frames per buffer. */
    var codecName: String? = null
        private set

    /**
     * Calls [onPcm] for each decoded block, in order, on a decoding thread.
     * [onProgress] receives the fraction decoded when the duration is known.
     * Returns early when [isCancelled] turns true.
     */
    fun decode(
        isCancelled: () -> Boolean,
        onProgress: (Double) -> Unit,
        onPcm: (PcmChunk) -> Unit,
    ) {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context, uri, null)
            val track = (0 until extractor.trackCount).firstOrNull {
                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: throw UnsupportedMediaException("no audio track")
            extractor.selectTrack(track)
            val format = extractor.getTrackFormat(track)
            val durationUs = if (format.containsKey(MediaFormat.KEY_DURATION)) format.getLong(MediaFormat.KEY_DURATION) else 0L
            val mime = format.getString(MediaFormat.KEY_MIME)!!
            val codec = try {
                MediaCodec.createDecoderByType(mime)
            } catch (e: Exception) {
                throw UnsupportedMediaException("no decoder for $mime: ${e.message}")
            }
            val thread = HandlerThread("sillok-decode").apply { start() }
            try {
                val batched = enableBatches(codec, mime, format)
                codecName = codec.name + if (batched) " (batched)" else ""
                Session(extractor, codec, batched, durationUs, isCancelled, onProgress, onPcm).run(format, Handler(thread.looper))
            } finally {
                codec.release()
                thread.quitSafely()
            }
        } finally {
            extractor.release()
        }
    }

    /** Asks the codec for batches of frames when it takes them. */
    private fun enableBatches(codec: MediaCodec, mime: String, format: MediaFormat): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) return false
        val capabilities = codec.codecInfo.getCapabilitiesForType(mime)
        if (!capabilities.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_MultipleFrames)) return false
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, BATCH_INPUT_BYTES)
        format.setInteger(MediaFormat.KEY_BUFFER_BATCH_MAX_OUTPUT_SIZE, BATCH_OUTPUT_BYTES)
        format.setInteger(MediaFormat.KEY_BUFFER_BATCH_THRESHOLD_OUTPUT_SIZE, BATCH_OUTPUT_BYTES / 2)
        return true
    }

    /** One decoding pass: the codec's callbacks run on a handler thread while [run] waits. */
    private class Session(
        val extractor: MediaExtractor,
        val codec: MediaCodec,
        val batched: Boolean,
        val durationUs: Long,
        val isCancelled: () -> Boolean,
        val onProgress: (Double) -> Unit,
        val onPcm: (PcmChunk) -> Unit,
    ) : MediaCodec.Callback() {
        private val done = CountDownLatch(1)
        @Volatile private var failure: Throwable? = null
        @Volatile private var stopped = false
        private var inputDone = false
        private var output: MediaFormat? = null

        fun run(format: MediaFormat, handler: Handler) {
            codec.setCallback(this, handler)
            try {
                codec.configure(format, null, null, 0)
            } catch (e: Exception) {
                throw UnsupportedMediaException("cannot decode ${format.getString(MediaFormat.KEY_MIME)}: ${e.message}")
            }
            codec.start()
            while (!done.await(100, TimeUnit.MILLISECONDS)) {
                if (isCancelled()) break
            }
            stopped = true
            codec.stop()
            // Callbacks already queued may still be running; the caller frees
            // what they use once this returns.
            val idle = CountDownLatch(1)
            handler.post { idle.countDown() }
            idle.await()
            failure?.let { throw it }
        }

        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
            if (stopped || inputDone) return
            guard { inputDone = !queueFrames(index) }
        }

        override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
            if (stopped) return
            guard { deliver(index, info.offset, info.size, info.presentationTimeUs, info.flags) }
        }

        // A batch's frames lie one after another in the buffer.
        override fun onOutputBuffersAvailable(codec: MediaCodec, index: Int, infos: ArrayDeque<MediaCodec.BufferInfo>) {
            if (stopped) return
            guard {
                val first = infos.first
                val last = infos.last
                val flags = infos.fold(0) { all, info -> all or info.flags }
                deliver(index, first.offset, last.offset + last.size - first.offset, last.presentationTimeUs, flags)
            }
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
            output = format
        }

        override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
            failure = e
            done.countDown()
        }

        private inline fun guard(block: () -> Unit) {
            try {
                block()
            } catch (e: Throwable) {
                failure = e
                stopped = true
                done.countDown()
            }
        }

        /** Fills an input buffer: one frame, or as many as fit when batched. Returns false at the end of the stream. */
        private fun queueFrames(index: Int): Boolean {
            val buffer = codec.getInputBuffer(index)!!
            val frames = ArrayDeque<MediaCodec.BufferInfo>()
            var offset = 0
            while (frames.size < if (batched) BATCH_FRAMES else 1) {
                val size = extractor.sampleSize.toInt()
                if (size < 0 || offset + size > buffer.capacity()) break
                val read = extractor.readSampleData(buffer, offset)
                frames.add(MediaCodec.BufferInfo().apply { set(offset, read, extractor.sampleTime, 0) })
                offset += read
                extractor.advance()
            }
            when {
                frames.isEmpty() -> {
                    codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    return false
                }
                batched && Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM -> codec.queueInputBuffers(index, frames)
                else -> frames.first.let { codec.queueInputBuffer(index, it.offset, it.size, it.presentationTimeUs, 0) }
            }
            return true
        }

        private fun deliver(index: Int, offset: Int, size: Int, timeUs: Long, flags: Int) {
            val format = output ?: codec.outputFormat
            if (size > 0) {
                val encoding = if (format.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                    format.getInteger(MediaFormat.KEY_PCM_ENCODING)
                } else {
                    AudioFormat.ENCODING_PCM_16BIT
                }
                if (encoding != AudioFormat.ENCODING_PCM_16BIT && encoding != AudioFormat.ENCODING_PCM_FLOAT) {
                    throw UnsupportedMediaException("PCM encoding $encoding")
                }
                onPcm(
                    PcmChunk(
                        codec.getOutputBuffer(index)!!, offset, size,
                        format.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                        format.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
                        encoding == AudioFormat.ENCODING_PCM_FLOAT,
                    ),
                )
                if (durationUs > 0) onProgress((timeUs.toDouble() / durationUs).coerceIn(0.0, 1.0))
            }
            codec.releaseOutputBuffer(index, false)
            if (flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) done.countDown()
        }
    }

    private companion object {
        // About a second of AAC per buffer: 48 frames of 1024 samples at 48 kHz.
        const val BATCH_FRAMES = 48
        const val BATCH_INPUT_BYTES = 64 * 1024
        // 48 frames of 2 channels of floats.
        const val BATCH_OUTPUT_BYTES = BATCH_FRAMES * 1024 * 2 * 4
    }
}
