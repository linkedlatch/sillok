package io.github.linkedlatch.sillok.engine

import org.json.JSONObject

/**
 * The transcript document of sillok_job_run(), read for display. The app
 * keeps the document itself as the source of truth and changes only speaker
 * names in it, so fields it does not show (words, models) survive.
 */
class Transcript(val json: String) {
    data class Speaker(val id: Int, val name: String?)
    data class Segment(val startMs: Long, val endMs: Long, val text: String, val speaker: Int?, val language: String = "")
    /** A model the run used: [device] as the library names it, and whether a GPU request fell back to the CPU. */
    data class ModelUse(val name: String, val device: String, val fellBackToCpu: Boolean)

    val durationMs: Long
    val speakers: List<Speaker>
    val segments: List<Segment>
    /** By role: "transcription", "voice_detection", "diarization". */
    val models: Map<String, ModelUse>

    init {
        val root = JSONObject(json)
        durationMs = root.getLong("duration_ms")
        val speakerArray = root.getJSONArray("speakers")
        speakers = List(speakerArray.length()) {
            val speaker = speakerArray.getJSONObject(it)
            Speaker(speaker.getInt("id"), speaker.optStringOrNull("name"))
        }
        segments = segments(root.getJSONArray("segments"))
        val modelObject = root.optJSONObject("models") ?: JSONObject()
        models = modelObject.keys().asSequence().associateWith { role ->
            val model = modelObject.getJSONObject(role)
            ModelUse(model.optString("name"), model.optString("device"), model.optBoolean("fell_back_to_cpu"))
        }
    }

    companion object {
        /** Segments of a JSON array as in the document. */
        fun segments(array: org.json.JSONArray): List<Segment> = List(array.length()) {
            val segment = array.getJSONObject(it)
            Segment(
                segment.getLong("start_ms"), segment.getLong("end_ms"), segment.getString("text"),
                if (segment.has("speaker")) segment.getInt("speaker") else null,
                segment.optString("language"),
            )
        }
    }

    fun speakerName(id: Int): String? = speakers.firstOrNull { it.id == id }?.name

    /** A copy with speakers renamed; a blank name clears it. */
    fun withSpeakerNames(names: Map<Int, String>): Transcript {
        val root = JSONObject(json)
        val array = root.getJSONArray("speakers")
        for (i in 0 until array.length()) {
            val speaker = array.getJSONObject(i)
            val name = names[speaker.getInt("id")] ?: continue
            speaker.put("name", name.trim().ifEmpty { JSONObject.NULL })
        }
        return Transcript(root.toString())
    }

    /** Index of the segment playing at [positionMs], or -1. */
    fun segmentAt(positionMs: Long): Int {
        var low = 0
        var high = segments.size - 1
        var found = -1
        while (low <= high) {
            val mid = (low + high) ushr 1
            if (segments[mid].startMs <= positionMs) {
                found = mid
                low = mid + 1
            } else {
                high = mid - 1
            }
        }
        return if (found >= 0 && positionMs < segments[found].endMs) found else -1
    }
}

private fun JSONObject.optStringOrNull(key: String): String? =
    if (isNull(key)) null else getString(key)
