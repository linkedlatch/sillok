package io.github.linkedlatch.sillok.engine

import java.nio.ByteBuffer

/**
 * The JNI functions of libsillok_jni.so, one per C ABI function of
 * include/sillok/sillok.h. Strings are
 * UTF-8 byte arrays; handles are pointers. [Sillok] wraps these for the app.
 */
internal object Native {
    init {
        System.loadLibrary("sillok_jni")
    }

    fun interface Progress {
        fun onProgress(stage: Int, fraction: Double)
    }

    external fun version(): ByteArray
    external fun modelCatalog(): ByteArray
    external fun modelCheck(dir: ByteArray, verifyHash: Boolean): ByteArray
    external fun devices(): ByteArray

    external fun engineCreate(config: ByteArray): Long
    external fun engineDestroy(engine: Long)

    external fun jobCreate(engine: Long, options: ByteArray): Long
    external fun jobPushAudio(
        job: Long, pcm: ByteBuffer, offset: Int, size: Int,
        sampleRate: Int, channels: Int, isFloat: Boolean,
    )
    external fun jobEndAudio(job: Long)
    external fun jobAudioLength(job: Long): Long
    external fun jobReadAudio(job: Long, start: Long, count: Int): ShortArray
    external fun jobRun(job: Long, progress: Progress): ByteArray
    external fun jobCancel(job: Long)
    external fun jobDestroy(job: Long)

    external fun export(transcript: ByteArray, format: ByteArray, options: ByteArray?): ByteArray
}
