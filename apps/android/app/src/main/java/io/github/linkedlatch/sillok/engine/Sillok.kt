package io.github.linkedlatch.sillok.engine

import java.io.File
import java.nio.ByteBuffer
import org.json.JSONArray
import org.json.JSONObject

/** A failed library call; [status] is a sillok_status value. */
class SillokException(val status: Int, message: String) : Exception(message) {
    // Called from JNI with the message as UTF-8.
    @Suppress("unused")
    constructor(status: Int, message: ByteArray) : this(status, message.decodeToString())

    companion object {
        const val MODEL_ERROR = 2
        const val CANCELLED = 3
    }
}

/** A file of a catalog model; [zip] files are optional accelerators (Apple only). */
data class ModelFile(val file: String, val url: String, val size: Long, val sha256: String, val zip: Boolean)

/**
 * A model of the catalog. [role] is "voice_detection", "transcription" or
 * "diarization"; [family] and [quantization] (e.g. "large-v3-turbo",
 * "q5_0") are for describing it.
 */
data class CatalogModel(
    val id: String,
    val role: String,
    val family: String,
    val quantization: String,
    val recommended: Boolean,
    val files: List<ModelFile>,
) {
    /** Bytes to download: the files the app uses (not the archives). */
    val size: Long get() = files.filter { !it.zip }.sumOf { it.size }
}

/** A compute device the library can use; [type] is "cpu", "gpu", "igpu", ... */
data class ComputeDevice(val name: String, val description: String, val type: String) {
    val isGpu get() = type == "gpu" || type == "igpu"
}

/** The processing stages a run reports, in order. */
enum class Stage { ANALYZE, TRANSCRIBE, DIARIZE }

private fun String.utf8() = encodeToByteArray()

object Sillok {
    val version: String get() = Native.version().decodeToString()

    fun catalog(): List<CatalogModel> = objects(Native.modelCatalog()).map { model ->
        val files = model.getJSONArray("files")
        CatalogModel(
            model.getString("id"), model.getString("role"), model.getString("family"),
            model.getString("quantization"), model.getBoolean("recommended"),
            List(files.length()) {
                val file = files.getJSONObject(it)
                ModelFile(file.getString("file"), file.getString("url"), file.getLong("size"),
                    file.getString("sha256"), file.getBoolean("zip"))
            },
        )
    }

    /** The models an engine uses: each role's recommended one, with [transcription] (when given) instead for transcription. */
    fun engineModels(transcription: String?): List<CatalogModel> = catalog().filter {
        if (it.role == "transcription" && transcription != null) it.id == transcription else it.recommended
    }

    /** Status of each catalog file in [dir] by file name: "ok", "missing", "wrong_size", ... */
    fun check(dir: File, verifyHash: Boolean): Map<String, String> =
        objects(Native.modelCheck(dir.path.utf8(), verifyHash))
            .associate { it.getString("file") to it.getString("status") }

    fun devices(): List<ComputeDevice> = objects(Native.devices()).map {
        ComputeDevice(it.getString("name"), it.getString("description"), it.getString("type"))
    }

    /** Renders [transcript] as "txt", "srt" or "json"; unnamed speakers get [speakerLabel] ("{n}" is the number). */
    fun export(transcript: String, format: String, speakerLabel: String): String {
        val options = JSONObject().put("speaker_label", speakerLabel).toString()
        return Native.export(transcript.utf8(), format.utf8(), options.utf8()).decodeToString()
    }

    private fun objects(json: ByteArray): List<JSONObject> {
        val array = JSONArray(json.decodeToString())
        return List(array.length()) { array.getJSONObject(it) }
    }
}

/**
 * sillok_engine: loads the models in [modelsDir] while a job runs. [gpuIndex]
 * null means CPU only; [transcriptionModel] null means the recommended one.
 */
class Engine(modelsDir: File, tempDir: File, gpuIndex: Int?, threads: Int, transcriptionModel: String? = null) : AutoCloseable {
    internal var handle: Long = Native.engineCreate(
        JSONObject()
            .put("models_dir", modelsDir.path)
            .put("temp_dir", tempDir.path)
            .put("device", if (gpuIndex != null) "gpu" else "cpu")
            .put("gpu_index", gpuIndex ?: 0)
            .put("threads", threads)
            .apply { if (transcriptionModel != null) put("transcription_model", transcriptionModel) }
            .toString().utf8(),
    )
        private set

    override fun close() {
        if (handle != 0L) Native.engineDestroy(handle)
        handle = 0
    }
}

/**
 * sillok_job: one recording. The job keeps it as 16 kHz mono, the audio the
 * transcript's times refer to, and the app reads it back for the listening
 * copy. [cancel] may be called from any thread.
 */
class Job(engine: Engine, language: String, diarize: Boolean, glossary: List<String>) : AutoCloseable {
    private val lock = Any()
    private var handle: Long = Native.jobCreate(
        engine.handle,
        JSONObject()
            .put("language", language)
            .put("diarize", diarize)
            .put("glossary", JSONArray(glossary))
            .toString().utf8(),
    )

    /** Appends decoded PCM: 16-bit integers, or 32-bit floats if [isFloat]. [pcm] must be direct. */
    fun pushAudio(pcm: ByteBuffer, offset: Int, size: Int, sampleRate: Int, channels: Int, isFloat: Boolean) =
        Native.jobPushAudio(handle, pcm, offset, size, sampleRate, channels, isFloat)

    /** Ends the recording; every sample of it becomes readable. */
    fun endAudio() = Native.jobEndAudio(handle)

    /** Samples of the job's recording (16 kHz mono, [SAMPLE_RATE]) available so far. */
    val audioLength: Long get() = Native.jobAudioLength(handle)

    /** Up to [count] samples of the job's recording from [start], as 16-bit PCM. */
    fun readAudio(start: Long, count: Int): ShortArray = Native.jobReadAudio(handle, start, count)

    /** Blocks until the transcript document is ready; [onProgress] runs on this thread. */
    fun run(onProgress: (Stage, Double) -> Unit): String =
        Native.jobRun(handle) { stage, fraction -> onProgress(Stage.entries[stage], fraction) }
            .decodeToString()

    fun cancel() = synchronized(lock) {
        if (handle != 0L) Native.jobCancel(handle)
    }

    /** Must not be called while [run] executes. */
    override fun close() = synchronized(lock) {
        if (handle != 0L) Native.jobDestroy(handle)
        handle = 0
    }

    companion object {
        /** SILLOK_SAMPLE_RATE */
        const val SAMPLE_RATE = 16_000
    }
}
