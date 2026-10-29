package io.github.linkedlatch.sillok.media

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaCodecList
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
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
            val codec = try {
                softwareDecoder(mime, format) ?: MediaCodec.createDecoderByType(mime).apply { configure(format, null, null, 0) }
            } catch (e: Exception) {
                throw UnsupportedMediaException("no decoder for $mime: ${e.message}")
            }
            codecName = codec.name
            try {
                codec.start()
                run(extractor, codec, durationUs, isCancelled, onProgress, onPcm)
            } finally {
                codec.release()
            }
        } finally {
            extractor.release()
        }
    }

    /**
     * A configured software decoder, if there is one for [mime]. Phones may
     * list a hardware audio decoder first, made for low-power playback;
     * decoding a whole file through it is much slower than through the
     * platform's software codec.
     */
    private fun softwareDecoder(mime: String, format: MediaFormat): MediaCodec? {
        val info = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull { info ->
            !info.isEncoder && info.isSoftwareOnly && !info.isAlias &&
                info.supportedTypes.any { it.equals(mime, ignoreCase = true) }
        } ?: return null
        val codec = MediaCodec.createByCodecName(info.name)
        return try {
            codec.configure(format, null, null, 0)
            codec
        } catch (e: Exception) {
            codec.release()
            null
        }
    }

    private fun run(
        extractor: MediaExtractor,
        codec: MediaCodec,
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
                    if (info.size > 0) {
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
                                codec.getOutputBuffer(index)!!, info.offset, info.size,
                                output.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                                output.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
                                encoding == AudioFormat.ENCODING_PCM_FLOAT,
                            ),
                        )
                        if (durationUs > 0) onProgress((info.presentationTimeUs.toDouble() / durationUs).coerceIn(0.0, 1.0))
                    }
                    codec.releaseOutputBuffer(index, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) return
                }
            }
        }
    }

    private companion object {
        const val TIMEOUT_US = 10_000L
    }
}
