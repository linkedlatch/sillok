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
import io.github.linkedlatch.sillok.data.JobEntry
import io.github.linkedlatch.sillok.data.JobSettings
import io.github.linkedlatch.sillok.data.JobStore
import io.github.linkedlatch.sillok.data.ModelDownloader
import io.github.linkedlatch.sillok.data.ModelIntegrityException
import io.github.linkedlatch.sillok.data.SettingsStore
import io.github.linkedlatch.sillok.data.Step
import io.github.linkedlatch.sillok.data.TranscriptionRunner
import io.github.linkedlatch.sillok.engine.ComputeDevice
import io.github.linkedlatch.sillok.engine.Sillok
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.media.UnsupportedMediaException
import java.io.File
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
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

/** Progress of the running transcription: the current step, its fraction and when it began. */
data class RunState(
    val name: String,
    val steps: List<Step>,
    val step: Step,
    val fraction: Double,
    val stepStartedAt: Long,
    val cancelling: Boolean = false,
)

class AppViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application
    private val modelsDir = File(app.filesDir, "models")
    private val downloader = ModelDownloader(modelsDir)
    private val store = JobStore(File(app.filesDir, "jobs"))
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
    private var runner: TranscriptionRunner? = null

    init {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { store.removeIncomplete() }
            refreshHistory()
            checkModels()
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

    /** Handles the back gesture; returns false when the app should close. */
    fun back(): Boolean {
        when (screen) {
            Screen.Home -> return false
            Screen.Running -> cancelRun()
            else -> screen = Screen.Home
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
        val steps = buildList {
            add(Step.READ)
            add(Step.ANALYZE)
            add(Step.TRANSCRIBE)
            if (settings.diarize) add(Step.DIARIZE)
        }
        val runner = TranscriptionRunner(app, store, modelsDir).also { runner = it }
        run = RunState(name, steps, Step.READ, 0.0, SystemClock.elapsedRealtime())
        screen = Screen.Running
        viewModelScope.launch {
            try {
                var lastStep = Step.READ
                var lastFraction = 0.0
                val entry = withContext(Dispatchers.Default) {
                    runner.run(uri, name, settings) { step, fraction ->
                        // Called often from the worker; pass on visible changes only.
                        if (step != lastStep || fraction - lastFraction >= 0.001 || fraction >= 1.0) {
                            lastStep = step
                            lastFraction = fraction
                            onMain { progress(step, fraction) }
                        }
                    }
                }
                refreshHistory()
                screen = if (entry != null) Screen.Result(entry.id) else Screen.Home
            } catch (e: Exception) {
                error = describe(e)
                screen = Screen.Home
            } finally {
                this@AppViewModel.runner = null
                run = null
            }
        }
    }

    private fun progress(step: Step, fraction: Double) {
        val current = run ?: return
        val now = SystemClock.elapsedRealtime()
        run = if (step != current.step) {
            current.copy(step = step, fraction = fraction, stepStartedAt = now)
        } else {
            current.copy(fraction = fraction)
        }
    }

    private fun onMain(block: () -> Unit) {
        viewModelScope.launch(Dispatchers.Main) { block() }
    }

    fun cancelRun() {
        val current = run ?: return
        run = current.copy(cancelling = true)
        runner?.cancel()
    }

    private fun describe(e: Exception): String = when (e) {
        is UnsupportedMediaException -> app.getString(R.string.error_unsupported_file, e.message ?: "")
        is SillokException ->
            if (e.status == SillokException.MODEL_ERROR) app.getString(R.string.error_model, e.message ?: "")
            else app.getString(R.string.error_engine, e.message ?: "")
        else -> app.getString(R.string.error_engine, e.message ?: e.javaClass.simpleName)
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

    override fun onCleared() {
        runner?.cancel()
    }
}
