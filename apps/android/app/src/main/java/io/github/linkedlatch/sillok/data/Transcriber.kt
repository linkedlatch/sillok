package io.github.linkedlatch.sillok.data

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.media.UnsupportedMediaException
import io.github.linkedlatch.sillok.service.Notifications
import io.github.linkedlatch.sillok.service.TranscriptionService
import java.io.File
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject

/** A file in the queue: waiting, or failed with [error] until retried or removed. */
data class QueueItem(
    val key: String,
    val uri: Uri,
    val name: String,
    val settings: JobSettings,
    val error: String? = null,
)

/** Progress of the running transcription: the current step, its fraction and when the job and the step began. */
data class RunState(
    val key: String,
    val name: String,
    val steps: List<Step>,
    val step: Step,
    val fraction: Double,
    val startedAt: Long,
    val stepStartedAt: Long,
    val cancelling: Boolean = false,
    /** How the read step went, once it is done. */
    val read: TranscriptionRunner.ReadTimings? = null,
) {
    /**
     * The whole job's fraction done. The steps weigh roughly what they take
     * on a CPU: transcription is most of it.
     */
    val overall: Double
        get() {
            val weights = steps.associateWith { weight(it) }
            val total = weights.values.sum()
            val before = steps.takeWhile { it != step }.sumOf { weights.getValue(it) }
            return ((before + weights.getValue(step) * fraction) / total).coerceIn(0.0, 1.0)
        }

    /** Time left for the whole job, extrapolated once enough is done to tell; null before. */
    fun remainingMs(now: Long): Long? {
        val elapsed = now - startedAt
        val done = overall
        if (cancelling || done < 0.03 || elapsed < 20_000) return null
        return (elapsed * (1 - done) / done).toLong()
    }

    private fun weight(step: Step) = when (step) {
        Step.READ -> 0.03
        Step.ANALYZE -> 0.02
        Step.TRANSCRIBE -> 0.85
        Step.DIARIZE -> 0.10
    }
}

/**
 * The queue of files to transcribe, worked through one at a time apart from
 * any screen, so that it goes on with the app in the background.
 * [TranscriptionService] keeps the process running meanwhile (G6). The queue
 * is saved, so that files left when the process ended wait for the next
 * start; [resume] continues with them.
 */
class Transcriber(private val app: Application) {
    val store = JobStore(File(app.filesDir, "jobs"))
    val modelsDir = File(app.filesDir, "models")
    private val queueFile = File(app.filesDir, "queue.json")

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val lock = Any()
    private val _queue = MutableStateFlow(loadQueue())
    private val _state = MutableStateFlow<RunState?>(null)
    private val _previews = MutableStateFlow<List<Transcript.Segment>>(emptyList())
    private val _finished = MutableStateFlow<Pair<String, String>?>(null)
    val queue: StateFlow<List<QueueItem>> = _queue.asStateFlow()
    val state: StateFlow<RunState?> = _state.asStateFlow()
    /** Segments of the running job so far, as the transcription finishes them. */
    val previews: StateFlow<List<Transcript.Segment>> = _previews.asStateFlow()
    /** The last file that finished: its queue key and its job id, for the screens to follow. */
    val finished: StateFlow<Pair<String, String>?> = _finished.asStateFlow()

    @Volatile private var runner: TranscriptionRunner? = null
    @Volatile private var timedOut = false
    private var working = false

    init {
        // Created once per process, before any job: leftovers are from a process that died.
        scope.launch(Dispatchers.IO) { store.removeIncomplete() }
    }

    /** Adds files with [settings] and starts on them; call from the foreground, which may start the service. */
    fun enqueue(files: List<Pair<Uri, String>>, settings: JobSettings) {
        val items = files.map { (uri, name) ->
            // Keeps reading the file allowed after the screen that picked it is gone.
            runCatching { app.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            QueueItem(UUID.randomUUID().toString(), uri, name, settings)
        }
        update { it + items }
        resume()
    }

    /** Works through the waiting files; call from the foreground. */
    fun resume() {
        synchronized(lock) {
            if (working || _queue.value.none { it.error == null }) return
            working = true
        }
        TranscriptionService.start(app)
        scope.launch { work() }
    }

    /** Puts a failed file back in line. */
    fun retry(key: String) {
        update { queue -> queue.map { if (it.key == key) it.copy(error = null) else it } }
        resume()
    }

    /** Removes a waiting or failed file; the running one is cancelled. */
    fun remove(key: String) {
        if (_state.value?.key == key) {
            cancel()
            return
        }
        _queue.value.firstOrNull { it.key == key }?.let { release(it.uri) }
        update { queue -> queue.filter { it.key != key } }
    }

    /** Cancels the running transcription; [timeout] when the system's time limit ends it, which keeps the file waiting. */
    fun cancel(timeout: Boolean = false) {
        if (timeout) timedOut = true
        _state.update { it?.copy(cancelling = true) }
        runner?.cancel()
    }

    private fun work() {
        while (true) {
            val item = _queue.value.firstOrNull { it.error == null }
            if (item == null) {
                synchronized(lock) { working = false }
                _state.value = null
                return
            }
            timedOut = false
            val outcome = runItem(item)
            when {
                outcome is Outcome.Done -> {
                    release(item.uri)
                    update { queue -> queue.filter { it.key != item.key } }
                    _finished.value = item.key to outcome.id
                    Notifications.finished(app, item.name, outcome.id, null)
                }
                timedOut -> {
                    // The file waits for the next time the app is opened.
                    update { queue -> queue.map { if (it.key == item.key) it.copy(error = app.getString(R.string.error_time_limit)) else it } }
                    Notifications.finished(app, item.name, null, app.getString(R.string.error_time_limit))
                    synchronized(lock) { working = false }
                    _state.value = null
                    return
                }
                outcome is Outcome.Failed -> {
                    update { queue -> queue.map { if (it.key == item.key) it.copy(error = outcome.message) else it } }
                    Notifications.finished(app, item.name, null, outcome.message)
                }
                else -> {
                    release(item.uri)
                    update { queue -> queue.filter { it.key != item.key } }
                }
            }
        }
    }

    private sealed interface Outcome {
        data class Done(val id: String) : Outcome
        data class Failed(val message: String) : Outcome
        data object Cancelled : Outcome
    }

    private fun runItem(item: QueueItem): Outcome {
        val steps = buildList {
            add(Step.READ)
            add(Step.ANALYZE)
            add(Step.TRANSCRIBE)
            if (item.settings.diarize) add(Step.DIARIZE)
        }
        val now = SystemClock.elapsedRealtime()
        _previews.value = emptyList()
        _state.value = RunState(item.key, item.name, steps, Step.READ, 0.0, now, now)
        val runner = TranscriptionRunner(app, store, modelsDir).also { runner = it }
        var lastFraction = 0.0
        return try {
            val entry = runner.run(
                item.uri, item.name, item.settings,
                onRead = { read -> _state.update { it?.copy(read = read) } },
                onSegments = { segments -> _previews.update { it + segments } },
            ) { step, fraction ->
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
            if (entry != null) Outcome.Done(entry.id) else Outcome.Cancelled
        } catch (e: Exception) {
            Outcome.Failed(describe(e))
        } finally {
            this.runner = null
            _previews.value = emptyList()
        }
    }

    private fun release(uri: Uri) {
        runCatching { app.contentResolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
    }

    private fun describe(e: Exception): String = when (e) {
        is UnsupportedMediaException -> app.getString(R.string.error_unsupported_file, e.message ?: "")
        is SillokException ->
            if (e.status == SillokException.MODEL_ERROR) app.getString(R.string.error_model, e.message ?: "")
            else app.getString(R.string.error_engine, e.message ?: "")
        else -> app.getString(R.string.error_engine, e.message ?: e.javaClass.simpleName)
    }

    // --- Saved queue ---------------------------------------------------------

    private fun update(change: (List<QueueItem>) -> List<QueueItem>) {
        synchronized(lock) {
            _queue.value = change(_queue.value)
            saveQueue(_queue.value)
        }
    }

    private fun saveQueue(items: List<QueueItem>) {
        val array = JSONArray()
        items.forEach { item ->
            array.put(
                JSONObject()
                    .put("key", item.key)
                    .put("uri", item.uri.toString())
                    .put("name", item.name)
                    .put("settings", item.settings.toJson())
                    .put("error", item.error),
            )
        }
        val temp = File(queueFile.parentFile, queueFile.name + ".tmp")
        temp.writeText(array.toString())
        temp.renameTo(queueFile)
    }

    private fun loadQueue(): List<QueueItem> = runCatching {
        val array = JSONArray(queueFile.readText())
        List(array.length()) {
            val item = array.getJSONObject(it)
            QueueItem(
                item.getString("key"), Uri.parse(item.getString("uri")), item.getString("name"),
                JobSettings.fromJson(item.getJSONObject("settings")),
                if (item.isNull("error")) null else item.getString("error"),
            )
        }
    }.getOrDefault(emptyList())
}
