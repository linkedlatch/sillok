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
import io.github.linkedlatch.sillok.data.RunOutcome
import io.github.linkedlatch.sillok.data.RunState
import io.github.linkedlatch.sillok.data.SettingsStore
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

sealed interface ModelState {
    data object Checking : ModelState
    data class Missing(val total: Long, val downloaded: Long, val error: String?) : ModelState
    data class Downloading(val total: Long, val downloaded: Long, val bytesPerSecond: Long) : ModelState
    data object Verifying : ModelState
    data object Ready : ModelState
}

sealed interface Screen {
    data object Home : Screen
    data class Setup(val uri: Uri, val name: String) : Screen
    data object Running : Screen
    data class Result(val id: String) : Screen
}

class AppViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application
    private val transcriber = (application as SillokApplication).transcriber
    private val downloader = ModelDownloader(transcriber.modelsDir)
    private val store = transcriber.store
    private val settingsStore = SettingsStore(app)

    var models by mutableStateOf<ModelState>(ModelState.Checking)
        private set
    var screen by mutableStateOf<Screen>(Screen.Home)
        private set
    var history by mutableStateOf<List<JobEntry>>(emptyList())
        private set
    var settings by mutableStateOf(settingsStore.load())
        private set
    var run by mutableStateOf<RunState?>(null)
        private set
    /** A failure to show once, then clear with [dismissError]. */
    var error by mutableStateOf<String?>(null)
        private set

    val devices: List<ComputeDevice> by lazy { Sillok.devices() }
    val version: String = Sillok.version

    private var download: Job? = null

    init {
        viewModelScope.launch {
            refreshHistory()
            checkModels()
        }
        // The job runs apart from this screen and may outlive it, or have
        // ended while no screen was open.
        if (transcriber.state.value != null) screen = Screen.Running
        viewModelScope.launch { transcriber.state.collect { run = it } }
        viewModelScope.launch {
            transcriber.outcome.filterNotNull().collect { outcome ->
                when (outcome) {
                    is RunOutcome.Done -> {
                        refreshHistory()
                        screen = Screen.Result(outcome.id)
                    }
                    is RunOutcome.Failed -> {
                        error = outcome.message
                        if (screen == Screen.Running) screen = Screen.Home
                    }
                    RunOutcome.Cancelled -> if (screen == Screen.Running) screen = Screen.Home
                }
                transcriber.consumeOutcome()
            }
        }
    }

    // --- Models -------------------------------------------------------------

    private suspend fun checkModels() {
        models = ModelState.Checking
        models = withContext(Dispatchers.IO) {
            val missing = downloader.missing()
            if (missing.isEmpty()) ModelState.Ready
            else ModelState.Missing(missing.sumOf { it.size }, downloader.downloaded(missing), null)
        }
    }

    fun startDownload() {
        if (download?.isActive == true) return
        download = viewModelScope.launch {
            val missing = withContext(Dispatchers.IO) { downloader.missing() }
            val total = missing.sumOf { it.size }
            var last = downloader.downloaded(missing)
            var lastAt = SystemClock.elapsedRealtime()
            var speed = 0L
            models = ModelState.Downloading(total, last, 0)
            try {
                withContext(Dispatchers.IO) {
                    downloader.download(missing) { done ->
                        val now = SystemClock.elapsedRealtime()
                        if (now - lastAt >= 500) {
                            speed = (done - last) * 1000 / (now - lastAt)
                            last = done
                            lastAt = now
                            val state = ModelState.Downloading(total, done, speed)
                            onMain { models = state }
                        }
                        if (done >= total) onMain { models = ModelState.Verifying }
                    }
                }
                models = ModelState.Ready
            } catch (e: CancellationException) {
                models = ModelState.Missing(total, downloader.downloaded(missing), null)
                throw e
            } catch (e: ModelIntegrityException) {
                models = ModelState.Missing(total, 0, app.getString(R.string.models_damaged))
                checkModels()
            } catch (e: IOException) {
                models = ModelState.Missing(total, downloader.downloaded(missing),
                    app.getString(R.string.models_download_failed, e.message ?: ""))
            }
        }
    }

    fun stopDownload() {
        download?.cancel()
    }

    /** Re-reads every model file and compares its hash with the catalog. */
    fun verifyModels() {
        if (run != null || download?.isActive == true) return
        viewModelScope.launch {
            models = ModelState.Verifying
            try {
                withContext(Dispatchers.IO) { downloader.verify() }
                models = ModelState.Ready
                error = app.getString(R.string.models_verified)
            } catch (e: ModelIntegrityException) {
                error = app.getString(R.string.models_damaged)
                checkModels()
            }
        }
    }

    // --- Navigation -----------------------------------------------------------

    fun open(uri: Uri) {
        val name = app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { if (it.moveToFirst()) it.getString(0) else null }
            ?: uri.lastPathSegment ?: "audio"
        screen = Screen.Setup(uri, name)
    }

    fun show(id: String) {
        screen = Screen.Result(id)
    }

    fun showRunning() {
        if (run != null) screen = Screen.Running
    }

    /** Handles the back gesture; returns false when the app should close. */
    fun back(): Boolean {
        when (screen) {
            Screen.Home -> return false
            else -> screen = Screen.Home  // a running job goes on in the background
        }
        return true
    }

    fun dismissError() {
        error = null
    }

    // --- Transcription -------------------------------------------------------

    fun start(uri: Uri, name: String, settings: JobSettings) {
        if (run != null) return
        this.settings = settings
        settingsStore.save(settings)
        transcriber.start(uri, name, settings)
        screen = Screen.Running
    }

    fun cancelRun() = transcriber.cancel()

    private fun onMain(block: () -> Unit) {
        viewModelScope.launch(Dispatchers.Main) { block() }
    }

    // --- History --------------------------------------------------------------

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
            if (screen == Screen.Result(id)) screen = Screen.Home
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
}
