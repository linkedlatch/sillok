package io.github.linkedlatch.sillok.media

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Build
import androidx.annotation.RequiresApi
import java.io.IOException
import java.nio.ByteBuffer

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
 * decoders (MediaExtractor + MediaCodec).
 */
class AudioDecoder(private val context: Context, private val uri: Uri) {
    /** Name of the codec the last [decode] used, for the run details. */
    var codecName: String? = null
        private set

    /**
     * Calls [onPcm] for each decoded block, in order. [onProgress] receives
     * the fraction decoded when the duration is known. Returns early when
     * [isCancelled] turns true.
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
            var batched = false
            val codec = try {
                MediaCodec.createDecoderByType(mime).apply {
                    batched = enableBatches(this, mime, format)
                    configure(format, null, null, 0)
                }
            } catch (e: Exception) {
                throw UnsupportedMediaException("no decoder for $mime: ${e.message}")
            }
            codecName = codec.name + if (batched) " (batched)" else ""
            try {
                codec.start()
                run(extractor, codec, batched, durationUs, isCancelled, onProgress, onPcm)
            } finally {
                codec.release()
            }
        } finally {
            extractor.release()
        }
    }

    /**
     * Has the codec take many frames per buffer when it can (Android 15+).
     * Each buffer is a round trip to the codec's process; one AAC frame per
     * buffer (23 ms) made decoding an hour take minutes on a phone.
     */
    private fun enableBatches(codec: MediaCodec, mime: String, format: MediaFormat): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) return false
        val capabilities = codec.codecInfo.getCapabilitiesForType(mime)
        if (!capabilities.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_MultipleFrames)) return false
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, BATCH_INPUT_BYTES)
        format.setInteger(MediaFormat.KEY_BUFFER_BATCH_MAX_OUTPUT_SIZE, BATCH_OUTPUT_BYTES)
        format.setInteger(MediaFormat.KEY_BUFFER_BATCH_THRESHOLD_OUTPUT_SIZE, BATCH_OUTPUT_BYTES / 2)
        return true
    }

    /** Fills one input buffer with as many frames as fit; returns false at the end of the stream. */
    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private fun queueBatch(codec: MediaCodec, extractor: MediaExtractor, index: Int): Boolean {
        val buffer = codec.getInputBuffer(index)!!
        val frames = java.util.ArrayDeque<MediaCodec.BufferInfo>()
        var offset = 0
        while (frames.size < BATCH_FRAMES) {
            val size = extractor.sampleSize.toInt()
            if (size < 0 || offset + size > buffer.capacity()) break
            val read = extractor.readSampleData(buffer, offset)
            frames.add(MediaCodec.BufferInfo().apply { set(offset, read, extractor.sampleTime, 0) })
            offset += read
            extractor.advance()
        }
        if (frames.isEmpty()) {
            codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            return false
        }
        codec.queueInputBuffers(index, frames)
        return true
    }

    private fun run(
        extractor: MediaExtractor,
        codec: MediaCodec,
        batched: Boolean,
        durationUs: Long,
        isCancelled: () -> Boolean,
        onProgress: (Double) -> Unit,
        onPcm: (PcmChunk) -> Unit,
    ) {
        val info = MediaCodec.BufferInfo()
        var inputDone = false
        var output = codec.outputFormat
        while (!isCancelled()) {
            // Fill every free input buffer before waiting for output. The codec
            // runs in another process; one frame in flight at a time would make
            // each frame (23 ms of AAC) cost a round trip, minutes for an hour.
            while (!inputDone) {
                val index = codec.dequeueInputBuffer(0)
                if (index < 0) break
                if (batched && Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                    inputDone = !queueBatch(codec, extractor, index)
                    continue
                }
                val size = extractor.readSampleData(codec.getInputBuffer(index)!!, 0)
                if (size < 0) {
                    codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    inputDone = true
                } else {
                    codec.queueInputBuffer(index, 0, size, extractor.sampleTime, 0)
                    extractor.advance()
                }
            }
            val index = codec.dequeueOutputBuffer(info, TIMEOUT_US)
            when {
                index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> output = codec.outputFormat
                index >= 0 -> {
                    // A batch's frames lie one after another; the info spans them all.
                    val offset = info.offset
                    val size = info.size
                    val timeUs = info.presentationTimeUs
                    if (size > 0) {
                        val encoding = if (output.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                            output.getInteger(MediaFormat.KEY_PCM_ENCODING)
                        } else {
                            AudioFormat.ENCODING_PCM_16BIT
                        }
                        if (encoding != AudioFormat.ENCODING_PCM_16BIT && encoding != AudioFormat.ENCODING_PCM_FLOAT) {
                            throw UnsupportedMediaException("PCM encoding $encoding")
                        }
                        onPcm(
                            PcmChunk(
                                codec.getOutputBuffer(index)!!, offset, size,
                                output.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                                output.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
                                encoding == AudioFormat.ENCODING_PCM_FLOAT,
                            ),
                        )
                        if (durationUs > 0) onProgress((timeUs.toDouble() / durationUs).coerceIn(0.0, 1.0))
                    }
                    codec.releaseOutputBuffer(index, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) return
                }
            }
        }
    }

    private companion object {
        const val TIMEOUT_US = 10_000L
        // About a second of AAC per buffer: 48 frames of 1024 samples at 48 kHz.
        const val BATCH_FRAMES = 48
        const val BATCH_INPUT_BYTES = 64 * 1024
        // 48 frames of 2 channels of floats.
        const val BATCH_OUTPUT_BYTES = BATCH_FRAMES * 1024 * 2 * 4
    }
}
