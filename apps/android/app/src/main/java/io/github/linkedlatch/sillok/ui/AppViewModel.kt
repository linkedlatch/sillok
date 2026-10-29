package io.github.linkedlatch.sillok.ui

import android.app.Application
import android.net.Uri
import android.os.SystemClock
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.SillokApplication
import io.github.linkedlatch.sillok.data.JobEntry
import io.github.linkedlatch.sillok.data.JobSettings
import io.github.linkedlatch.sillok.data.ModelDownloader
import io.github.linkedlatch.sillok.data.ModelIntegrityException
import io.github.linkedlatch.sillok.data.QueueItem
import io.github.linkedlatch.sillok.data.RunState
import io.github.linkedlatch.sillok.data.SettingsStore
import io.github.linkedlatch.sillok.engine.CatalogModel
import io.github.linkedlatch.sillok.engine.ComputeDevice
import io.github.linkedlatch.sillok.engine.Sillok
import io.github.linkedlatch.sillok.engine.Transcript
import java.io.File
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Whether a catalog model is on the device. */
sealed interface ModelStatus {
    data object Ready : ModelStatus
    data class Missing(val downloaded: Long) : ModelStatus
    data class Downloading(val done: Long, val total: Long, val bytesPerSecond: Long) : ModelStatus
    data object Verifying : ModelStatus
}

/** One row of the library: a running, waiting, failed or finished transcription. */
sealed interface LibraryItem {
    val key: String

    data class Running(val run: RunState) : LibraryItem {
        override val key get() = run.key
    }

    data class Waiting(val item: QueueItem, val position: Int) : LibraryItem {
        override val key get() = item.key
    }

    data class Failed(val item: QueueItem) : LibraryItem {
        override val key get() = item.key
    }

    data class Done(val entry: JobEntry) : LibraryItem {
        override val key get() = entry.id
    }
}

/** Files picked for adding, while the add sheet is open. */
data class PendingFiles(val files: List<Pair<Uri, String>>)

class AppViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application
    private val transcriber = (application as SillokApplication).transcriber
    private val downloader = ModelDownloader(transcriber.modelsDir)
    private val store = transcriber.store
    private val settingsStore = SettingsStore(app)

    val catalog: List<CatalogModel> = Sillok.catalog()
    val devices: List<ComputeDevice> by lazy { Sillok.devices() }
    val gpus: List<ComputeDevice> get() = devices.filter { it.isGpu }

    var models by mutableStateOf<Map<String, ModelStatus>>(emptyMap())
        private set
    /** Why the model files could not be checked; the app cannot start without them. */
    var modelsError by mutableStateOf<String?>(null)
        private set
    /** False until the first check of the model files is done. */
    var modelsChecked by mutableStateOf(false)
        private set
    var history by mutableStateOf<List<JobEntry>>(emptyList())
        private set
    var queue by mutableStateOf<List<QueueItem>>(emptyList())
        private set
    var run by mutableStateOf<RunState?>(null)
        private set
    var previews by mutableStateOf<List<Transcript.Segment>>(emptyList())
        private set
    var defaults by mutableStateOf(settingsStore.load())
        private set
    var pending by mutableStateOf<PendingFiles?>(null)
        private set
    var showSettings by mutableStateOf(false)
        private set
    /** The last file that finished (queue key to job id), so that a screen showing it can follow. */
    var finished by mutableStateOf<Pair<String, String>?>(null)
        private set
    /** A job to show, from a notification. */
    var openRequest by mutableStateOf<String?>(null)
        private set
    /** A message to show once, then clear with [dismissMessage]. */
    var message by mutableStateOf<String?>(null)
        private set
    /** Why the app ended abnormally last time, to show once and share. */
    var crashReport by mutableStateOf<String?>(null)
        private set

    val version: String = Sillok.version

    private var download: Job? = null

    init {
        viewModelScope.launch {
            crashReport = withContext(Dispatchers.IO) { (app as SillokApplication).crashLog.takeReport() }
            refreshHistory()
            checkModels()
        }
        viewModelScope.launch { transcriber.state.collect { run = it } }
        viewModelScope.launch { transcriber.queue.collect { queue = it } }
        viewModelScope.launch { transcriber.previews.collect { previews = it } }
        viewModelScope.launch {
            transcriber.finished.filterNotNull().collect {
                refreshHistory()
                finished = it
            }
        }
        // Files left waiting when the process ended continue now that the app is in front.
        transcriber.resume()
    }

    /** The library: the running job, waiting and failed files, then the history. */
    val library: List<LibraryItem>
        get() = buildList {
            run?.let { add(LibraryItem.Running(it)) }
            var position = 0
            queue.forEach { item ->
                when {
                    item.key == run?.key -> Unit
                    item.error != null -> add(LibraryItem.Failed(item))
                    else -> add(LibraryItem.Waiting(item, ++position))
                }
            }
            history.forEach { add(LibraryItem.Done(it)) }
        }

    // --- Models -------------------------------------------------------------

    fun transcriptionModels(): List<CatalogModel> = catalog.filter { it.role == "transcription" }

    fun recommendedTranscription(): CatalogModel = transcriptionModels().first { it.recommended }

    /** Whether a job can run with [transcriptionModel] (null: the recommended one). */
    fun modelsReady(transcriptionModel: String? = defaults.transcriptionModel): Boolean =
        Sillok.engineModels(transcriptionModel).all { models[it.id] == ModelStatus.Ready }

    /** Transcription models on the device, to choose from. */
    fun readyTranscriptionModels(): List<CatalogModel> = transcriptionModels().filter { models[it.id] == ModelStatus.Ready }

    private suspend fun checkModels() {
        try {
            val status = withContext(Dispatchers.IO) { Sillok.check(transcriber.modelsDir, verifyHash = false) }
            models = catalog.associate { model ->
                val files = model.files.filter { !it.zip }
                model.id to if (files.all { status[it.file] == "ok" }) ModelStatus.Ready
                else ModelStatus.Missing(downloader.downloaded(files.filter { status[it.file] != "ok" }))
            }
            modelsError = null
            modelsChecked = true
        } catch (e: Exception) {
            if (e is CancellationException) throw e
            // The start screen shows this with a way to try again instead of nothing.
            modelsError = e.message ?: e.javaClass.simpleName
        }
    }

    /** Checks the model files again after [modelsError]. */
    fun retryModelCheck() {
        modelsError = null
        viewModelScope.launch { checkModels() }
    }

    /** Downloads [modelId] with the other roles' models it needs. One download at a time. */
    fun downloadModel(modelId: String) {
        if (download?.isActive == true) return
        val model = catalog.first { it.id == modelId }
        download = viewModelScope.launch {
            val files = withContext(Dispatchers.IO) {
                downloader.missing(if (model.role == "transcription") model.id else null)
            }
            val total = files.sumOf { it.size }
            var last = downloader.downloaded(files)
            var lastAt = SystemClock.elapsedRealtime()
            models = models + (modelId to ModelStatus.Downloading(last, total, 0))
            try {
                withContext(Dispatchers.IO) {
                    downloader.download(files) { done ->
                        val now = SystemClock.elapsedRealtime()
                        if (now - lastAt >= 500 || done >= total) {
                            val speed = if (now > lastAt) (done - last) * 1000 / (now - lastAt) else 0
                            last = done
                            lastAt = now
                            val state = if (done >= total) ModelStatus.Verifying else ModelStatus.Downloading(done, total, speed)
                            viewModelScope.launch(Dispatchers.Main) { models = models + (modelId to state) }
                        }
                    }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: ModelIntegrityException) {
                message = app.getString(R.string.models_damaged)
            } catch (e: IOException) {
                message = app.getString(R.string.models_download_failed, e.message ?: "")
            } finally {
                withContext(kotlinx.coroutines.NonCancellable) { checkModels() }
                transcriber.resume()
            }
        }
    }

    fun stopDownload() {
        download?.cancel()
    }

    /** Removes a transcription model's files; not the one the defaults use. */
    fun deleteModel(modelId: String) {
        if (modelId == (defaults.transcriptionModel ?: recommendedTranscription().id)) return
        val keep = catalog.filter { it.id != modelId && models[it.id] == ModelStatus.Ready }
            .flatMap { it.files }.map { it.file }.toSet()
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                catalog.first { it.id == modelId }.files.filter { it.file !in keep }
                    .forEach { File(transcriber.modelsDir, it.file).delete() }
            }
            checkModels()
        }
    }

    /** Re-reads every model file and compares its hash with the catalog. */
    fun verifyModels() {
        if (run != null || download?.isActive == true) return
        viewModelScope.launch {
            message = try {
                withContext(Dispatchers.IO) { downloader.verify() }
                app.getString(R.string.models_verified)
            } catch (e: ModelIntegrityException) {
                app.getString(R.string.models_damaged)
            }
            checkModels()
        }
    }

    /** Bytes the models and the history take on the device. */
    suspend fun storageUse(): Pair<Long, Long> = withContext(Dispatchers.IO) {
        transcriber.modelsDir.walkTopDown().filter { it.isFile }.sumOf { it.length() } to
            File(app.filesDir, "jobs").walkTopDown().filter { it.isFile }.sumOf { it.length() }
    }

    // --- Adding files ---------------------------------------------------------

    fun pick(uris: List<Uri>) {
        if (uris.isEmpty()) return
        pending = PendingFiles(uris.map { uri ->
            val name = app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { if (it.moveToFirst()) it.getString(0) else null }
                ?: uri.lastPathSegment ?: "audio"
            uri to name
        })
    }

    fun cancelPending() {
        pending = null
    }

    /** Queues the picked files with [settings] (the defaults, or changed for these files). */
    fun start(settings: JobSettings) {
        val files = pending?.files ?: return
        pending = null
        transcriber.enqueue(files, settings)
    }

    fun saveDefaults(settings: JobSettings) {
        defaults = settings
        settingsStore.save(settings)
    }

    // --- Queue ------------------------------------------------------------------

    fun cancelRun() = transcriber.cancel()
    fun retry(key: String) = transcriber.retry(key)
    fun remove(key: String) = transcriber.remove(key)

    // --- History ------------------------------------------------------------------

    private suspend fun refreshHistory() {
        history = withContext(Dispatchers.IO) { store.list() }
    }

    fun entry(id: String): JobEntry? = history.firstOrNull { it.id == id }

    suspend fun loadTranscript(id: String): Transcript? = withContext(Dispatchers.IO) {
        runCatching { Transcript(store.transcriptFile(id).readText()) }.getOrNull()
    }

    fun audioFile(id: String): File = store.audioFile(id)

    fun rename(entry: JobEntry, title: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { store.rename(entry, title.trim().ifEmpty { entry.title }) }
            refreshHistory()
        }
    }

    fun delete(id: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { store.delete(id) }
            refreshHistory()
        }
    }

    suspend fun saveSpeakerNames(id: String, transcript: Transcript, names: Map<Int, String>): Transcript {
        val renamed = transcript.withSpeakerNames(names)
        withContext(Dispatchers.IO) { store.saveTranscript(id, renamed.json) }
        return renamed
    }

    /** Writes [transcript] as [format] to a file the share sheet can hand to other apps. */
    suspend fun export(entry: JobEntry, transcript: Transcript, format: String): File = withContext(Dispatchers.IO) {
        val text = Sillok.export(transcript.json, format, app.getString(R.string.speaker_label))
        val dir = File(app.cacheDir, "export").apply { deleteRecursively(); mkdirs() }
        val safe = entry.title.replace(Regex("[\\\\/:*?\"<>|]"), "_").take(80).ifBlank { "transcript" }
        File(dir, "$safe.$format").apply { writeText(text) }
    }

    // --- Screens --------------------------------------------------------------------

    fun requestOpen(id: String?) {
        openRequest = id
    }

    fun openSettings(open: Boolean) {
        showSettings = open
    }

    fun dismissMessage() {
        message = null
    }

    fun dismissCrashReport() {
        crashReport = null
    }
}
