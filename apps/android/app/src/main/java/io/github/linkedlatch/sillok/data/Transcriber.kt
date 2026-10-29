package io.github.linkedlatch.sillok.data

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.media.UnsupportedMediaException
import io.github.linkedlatch.sillok.service.Notifications
import io.github.linkedlatch.sillok.service.TranscriptionService
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Progress of the running transcription: the current step, its fraction and when it began. */
data class RunState(
    val name: String,
    val steps: List<Step>,
    val step: Step,
    val fraction: Double,
    val stepStartedAt: Long,
    val cancelling: Boolean = false,
)

/** How the last transcription ended, until the app has shown it. */
sealed interface RunOutcome {
    data class Done(val id: String) : RunOutcome
    data class Failed(val message: String) : RunOutcome
    data object Cancelled : RunOutcome
}

/**
 * Runs transcriptions for the whole app, apart from any screen, so that work
 * goes on while the app is in the background. [TranscriptionService] keeps
 * the process running meanwhile (docs/GUI-DESIGN.md G6).
 */
class Transcriber(private val app: Application) {
    val store = JobStore(File(app.filesDir, "jobs"))
    val modelsDir = File(app.filesDir, "models")

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val _state = MutableStateFlow<RunState?>(null)
    private val _outcome = MutableStateFlow<RunOutcome?>(null)
    val state: StateFlow<RunState?> = _state.asStateFlow()
    val outcome: StateFlow<RunOutcome?> = _outcome.asStateFlow()

    @Volatile private var runner: TranscriptionRunner? = null
    @Volatile private var timedOut = false

    init {
        // Created once per process, before any job: leftovers are from a process that died.
        scope.launch(Dispatchers.IO) { store.removeIncomplete() }
    }

    /** Starts a transcription; call from the foreground, which may start the service. */
    fun start(uri: Uri, name: String, settings: JobSettings) {
        if (_state.value != null) return
        val steps = buildList {
            add(Step.READ)
            add(Step.ANALYZE)
            add(Step.TRANSCRIBE)
            if (settings.diarize) add(Step.DIARIZE)
        }
        // Keeps reading the file allowed after the screen that picked it is gone.
        val persisted = runCatching {
            app.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }.isSuccess
        val runner = TranscriptionRunner(app, store, modelsDir).also { runner = it }
        timedOut = false
        _outcome.value = null
        _state.value = RunState(name, steps, Step.READ, 0.0, SystemClock.elapsedRealtime())
        TranscriptionService.start(app)

        scope.launch {
            var lastFraction = 0.0
            val outcome = try {
                val entry = runner.run(uri, name, settings) { step, fraction ->
                    // Called often from the worker; pass on visible changes only.
                    val current = _state.value ?: return@run
                    if (step != current.step) {
                        lastFraction = fraction
                        _state.update { it?.copy(step = step, fraction = fraction, stepStartedAt = SystemClock.elapsedRealtime()) }
                    } else if (fraction - lastFraction >= 0.001 || fraction >= 1.0) {
                        lastFraction = fraction
                        _state.update { it?.copy(fraction = fraction) }
                    }
                }
                when {
                    entry != null -> RunOutcome.Done(entry.id)
                    timedOut -> RunOutcome.Failed(app.getString(R.string.error_time_limit))
                    else -> RunOutcome.Cancelled
                }
            } catch (e: Exception) {
                RunOutcome.Failed(describe(e))
            } finally {
                if (persisted) {
                    runCatching {
                        app.contentResolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                }
            }
            this@Transcriber.runner = null
            _outcome.value = outcome
            _state.value = null
            Notifications.finished(app, name, outcome)
        }
    }

    /** Cancels the running transcription; [timeout] when the system's time limit ends it. */
    fun cancel(timeout: Boolean = false) {
        if (timeout) timedOut = true
        _state.update { it?.copy(cancelling = true) }
        runner?.cancel()
    }

    /** Marks the outcome as shown. */
    fun consumeOutcome() {
        _outcome.value = null
    }

    private fun describe(e: Exception): String = when (e) {
        is UnsupportedMediaException -> app.getString(R.string.error_unsupported_file, e.message ?: "")
        is SillokException ->
            if (e.status == SillokException.MODEL_ERROR) app.getString(R.string.error_model, e.message ?: "")
            else app.getString(R.string.error_engine, e.message ?: "")
        else -> app.getString(R.string.error_engine, e.message ?: e.javaClass.simpleName)
    }
}
