package io.github.linkedlatch.sillok.data

import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.json.JSONObject

/** A finished transcription in the history. */
data class JobEntry(
    val id: String,
    val title: String,
    val source: String,
    val createdMs: Long,
    val durationMs: Long,
)

/**
 * The history, one folder per job under [root] (docs/GUI-DESIGN.md G7):
 * transcript.json (the library's document, with speaker names), audio.m4a
 * (the listening copy) and meta.json. A folder counts as a job once its
 * meta.json exists, which is written last.
 */
class JobStore(private val root: File) {
    fun list(): List<JobEntry> =
        (root.listFiles() ?: emptyArray())
            .mapNotNull { dir -> runCatching { read(dir) }.getOrNull() }
            .sortedByDescending { it.createdMs }

    /** A new, empty folder for a job that is about to run. */
    fun create(): File {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
        var dir: File
        var n = 0
        do {
            dir = File(root, if (n == 0) stamp else "$stamp-$n")
            n++
        } while (dir.exists())
        dir.mkdirs()
        return dir
    }

    fun complete(dir: File, transcript: String, source: String, durationMs: Long): JobEntry {
        writeAtomically(transcriptFile(dir.name), transcript)
        val entry = JobEntry(dir.name, source.substringBeforeLast('.').ifEmpty { source }, source,
            System.currentTimeMillis(), durationMs)
        write(entry)
        return entry
    }

    fun get(id: String): JobEntry? = runCatching { read(File(root, id)) }.getOrNull()

    fun rename(entry: JobEntry, title: String) = write(entry.copy(title = title))

    fun delete(id: String) {
        File(root, id).deleteRecursively()
    }

    /** Removes folders of jobs that never finished; call when no job is running. */
    fun removeIncomplete() {
        root.listFiles()?.filter { !File(it, META).exists() }?.forEach { it.deleteRecursively() }
    }

    fun transcriptFile(id: String) = File(File(root, id), TRANSCRIPT)
    fun audioFile(id: String) = File(File(root, id), AUDIO)

    fun saveTranscript(id: String, transcript: String) = writeAtomically(transcriptFile(id), transcript)

    private fun read(dir: File): JobEntry {
        val json = JSONObject(File(dir, META).readText())
        return JobEntry(dir.name, json.getString("title"), json.getString("source"),
            json.getLong("created_ms"), json.getLong("duration_ms"))
    }

    private fun write(entry: JobEntry) {
        val json = JSONObject()
            .put("title", entry.title)
            .put("source", entry.source)
            .put("created_ms", entry.createdMs)
            .put("duration_ms", entry.durationMs)
        writeAtomically(File(File(root, entry.id), META), json.toString(2))
    }

    private fun writeAtomically(file: File, text: String) {
        val temp = File(file.parentFile, file.name + ".tmp")
        temp.writeText(text)
        if (!temp.renameTo(file)) throw java.io.IOException("cannot write ${file.path}")
    }

    companion object {
        const val TRANSCRIPT = "transcript.json"
        const val AUDIO = "audio.m4a"
        private const val META = "meta.json"
    }
}
