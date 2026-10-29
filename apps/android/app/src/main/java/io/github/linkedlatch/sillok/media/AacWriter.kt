package io.github.linkedlatch.sillok.media

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import java.io.File
import java.nio.ByteOrder

/**
 * Encodes 16-bit PCM (interleaved if [channels] > 1) as AAC-LC into an .m4a
 * file with the platform encoder: the compressed copy the app plays
 * transcript segments from.
 */
class AacWriter(
    private val file: File,
    private val sampleRate: Int,
    bitRate: Int = 24_000,
    private val channels: Int = 1,
) : AutoCloseable {
    private val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
    private val muxer = MediaMuxer(file.path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
    private val info = MediaCodec.BufferInfo()
    private var track = -1
    private var frames = 0L
    private var released = false

    init {
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels).apply {
            setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, bitRate)
            // A second of samples per input buffer: each buffer is a round
            // trip to the codec's process.
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, sampleRate * channels * 2)
        }
        try {
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            codec.start()
        } catch (e: Exception) {
            close()
            throw e
        }
    }

    fun write(pcm: ShortArray) {
        var position = 0
        while (position < pcm.size) {
            val index = codec.dequeueInputBuffer(TIMEOUT_US)
            if (index >= 0) {
                val buffer = codec.getInputBuffer(index)!!
                buffer.clear()
                // Whole frames, so that timestamps stay exact.
                val count = minOf(pcm.size - position, buffer.remaining() / 2 / channels * channels)
                buffer.order(ByteOrder.nativeOrder()).asShortBuffer().put(pcm, position, count)
                codec.queueInputBuffer(index, 0, count * 2, frames * 1_000_000 / sampleRate, 0)
                frames += count / channels
                position += count
            }
            drain(endOfStream = false)
        }
    }

    /** Ends the stream and completes the file. Returns false if no audio was written. */
    fun finish(): Boolean {
        while (true) {
            val index = codec.dequeueInputBuffer(TIMEOUT_US)
            if (index >= 0) {
                codec.queueInputBuffer(index, 0, 0, frames * 1_000_000 / sampleRate, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                break
            }
            drain(endOfStream = false)
        }
        drain(endOfStream = true)
        val written = track >= 0 && frames > 0
        if (track >= 0) muxer.stop()
        close()
        return written
    }

    private fun drain(endOfStream: Boolean) {
        while (true) {
            val index = codec.dequeueOutputBuffer(info, if (endOfStream) TIMEOUT_US else 0)
            when {
                index == MediaCodec.INFO_TRY_AGAIN_LATER -> if (!endOfStream) return
                index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    track = muxer.addTrack(codec.outputFormat)
                    muxer.start()
                }
                index >= 0 -> {
                    val isConfig = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
                    if (!isConfig && info.size > 0 && track >= 0) {
                        val buffer = codec.getOutputBuffer(index)!!
                        buffer.position(info.offset)
                        buffer.limit(info.offset + info.size)
                        muxer.writeSampleData(track, buffer, info)
                    }
                    codec.releaseOutputBuffer(index, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) return
                }
            }
        }
    }

    override fun close() {
        if (released) return
        released = true
        codec.release()
        muxer.release()
    }

    private companion object {
        const val TIMEOUT_US = 10_000L
    }
}
