package io.github.linkedlatch.sillok.data

import android.content.Context

/** Transcription settings, kept between runs. [gpuIndex] null means CPU. */
data class JobSettings(
    val language: String = "auto",
    val diarize: Boolean = true,
    val glossary: String = "",
    val gpuIndex: Int? = null,
) {
    /** Glossary terms, one per line or separated by commas. */
    val terms: List<String>
        get() = glossary.split('\n', ',').map { it.trim() }.filter { it.isNotEmpty() }
}

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    fun load() = JobSettings(
        language = prefs.getString("language", null) ?: "auto",
        diarize = prefs.getBoolean("diarize", true),
        glossary = prefs.getString("glossary", null) ?: "",
        gpuIndex = prefs.getInt("gpu_index", -1).takeIf { it >= 0 },
    )

    fun save(settings: JobSettings) {
        prefs.edit()
            .putString("language", settings.language)
            .putBoolean("diarize", settings.diarize)
            .putString("glossary", settings.glossary)
            .putInt("gpu_index", settings.gpuIndex ?: -1)
            .apply()
    }
}
