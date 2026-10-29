package io.github.linkedlatch.sillok.data

import android.content.Context
import org.json.JSONObject

/**
 * Transcription settings: the defaults kept between runs, and a queued
 * file's own. [gpuIndex] null means the CPU; [transcriptionModel] null the
 * recommended model.
 */
data class JobSettings(
    val language: String = "auto",
    val diarize: Boolean = true,
    val glossary: String = "",
    val gpuIndex: Int? = null,
    val transcriptionModel: String? = null,
) {
    /** Glossary terms, one per line or separated by commas. */
    val terms: List<String>
        get() = glossary.split('\n', ',').map { it.trim() }.filter { it.isNotEmpty() }

    fun toJson(): JSONObject = JSONObject()
        .put("language", language)
        .put("diarize", diarize)
        .put("glossary", glossary)
        .put("gpu_index", gpuIndex ?: -1)
        .put("transcription_model", transcriptionModel)

    companion object {
        fun fromJson(json: JSONObject) = JobSettings(
            language = json.optString("language", "auto"),
            diarize = json.optBoolean("diarize", true),
            glossary = json.optString("glossary"),
            gpuIndex = json.optInt("gpu_index", -1).takeIf { it >= 0 },
            transcriptionModel = if (json.isNull("transcription_model")) null else json.optString("transcription_model").ifEmpty { null },
        )
    }
}

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    fun load(): JobSettings =
        prefs.getString("defaults", null)?.let { runCatching { JobSettings.fromJson(JSONObject(it)) }.getOrNull() }
            // Before the defaults were one value.
            ?: JobSettings(
                language = prefs.getString("language", null) ?: "auto",
                diarize = prefs.getBoolean("diarize", true),
                glossary = prefs.getString("glossary", null) ?: "",
                gpuIndex = prefs.getInt("gpu_index", -1).takeIf { it >= 0 },
            )

    fun save(settings: JobSettings) {
        prefs.edit().putString("defaults", settings.toJson().toString()).apply()
    }
}
