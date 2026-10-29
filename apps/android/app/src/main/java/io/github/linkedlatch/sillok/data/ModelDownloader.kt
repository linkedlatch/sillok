package io.github.linkedlatch.sillok.data

import io.github.linkedlatch.sillok.engine.ModelFile
import io.github.linkedlatch.sillok.engine.Sillok
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

class ModelIntegrityException(val files: List<String>) : IOException("damaged: $files")

/**
 * Downloads the model catalog into [dir] (docs/DESIGN.md D9). Each file
 * downloads to "<name>.part" and resumes from there; hashes are checked once
 * every file is complete, and damaged files are removed so the next attempt
 * downloads them again.
 */
class ModelDownloader(private val dir: File) {
    /**
     * Files of the engine's models (see [Sillok.engineModels]) that are not
     * complete in [dir]. Archives are left out: only Apple builds use them.
     */
    fun missing(transcriptionModel: String? = null): List<ModelFile> {
        val status = Sillok.check(dir, verifyHash = false)
        return Sillok.engineModels(transcriptionModel).flatMap { it.files }
            .filter { !it.zip && status[it.file] != "ok" }
            .distinctBy { it.file }
    }

    /** Bytes already downloaded of [files], counting partial downloads. */
    fun downloaded(files: List<ModelFile>): Long = files.sumOf { partFile(it).length().coerceAtMost(it.size) }

    suspend fun download(files: List<ModelFile>, onProgress: (Long) -> Unit) {
        dir.mkdirs()
        var done = downloaded(files)
        for (model in files) {
            downloadFile(model) { bytes ->
                done += bytes
                onProgress(done)
            }
        }
        verify()
    }

    /** Checks every file's hash; removes and reports damaged ones. */
    fun verify() {
        // Only files that are there: other models' files are not downloaded.
        val damaged = Sillok.check(dir, verifyHash = true)
            .filter { (file, status) -> status != "ok" && status != "missing" && File(dir, file).exists() }.keys.toList()
        damaged.forEach { File(dir, it).delete() }
        if (damaged.isNotEmpty()) throw ModelIntegrityException(damaged)
    }

    private fun partFile(model: ModelFile) = File(dir, model.file + ".part")

    private suspend fun downloadFile(model: ModelFile, onBytes: (Long) -> Unit) {
        val target = File(dir, model.file)
        val part = partFile(model)
        target.delete()
        if (part.length() > model.size) part.delete()
        if (part.length() < model.size) {
            val start = part.length()
            val connection = URL(model.url).openConnection() as HttpURLConnection
            try {
                connection.connectTimeout = 15_000
                connection.readTimeout = 30_000
                if (start > 0) connection.setRequestProperty("Range", "bytes=$start-")
                val append = when (connection.responseCode) {
                    HttpURLConnection.HTTP_PARTIAL -> true
                    HttpURLConnection.HTTP_OK -> false
                    else -> throw IOException("HTTP ${connection.responseCode} for ${model.file}")
                }
                if (!append && start > 0) onBytes(-start)  // the server restarts from the beginning
                connection.inputStream.use { input ->
                    FileOutputStream(part, append).use { output ->
                        val buffer = ByteArray(1 shl 16)
                        while (true) {
                            currentCoroutineContext().ensureActive()
                            val read = input.read(buffer)
                            if (read < 0) break
                            output.write(buffer, 0, read)
                            onBytes(read.toLong())
                        }
                    }
                }
            } finally {
                connection.disconnect()
            }
        }
        if (part.length() != model.size) throw IOException("incomplete download of ${model.file}")
        if (!part.renameTo(target)) throw IOException("cannot rename ${part.name}")
    }
}
