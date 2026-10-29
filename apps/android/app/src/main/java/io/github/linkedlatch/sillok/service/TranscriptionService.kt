package io.github.linkedlatch.sillok.service

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.content.ContextCompat
import io.github.linkedlatch.sillok.SillokApplication
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Keeps the process and the CPU running while a transcription runs, so that
 * it continues with the app in the background or the screen off, and shows
 * its progress in a notification with a cancel button.
 */
class TranscriptionService : Service() {
    private val scope = MainScope()
    private var watcher: Job? = null
    private var wakeLock: PowerManager.WakeLock? = null

    private val transcriber get() = (application as SillokApplication).transcriber

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_CANCEL) {
            transcriber.cancel()
            return START_NOT_STICKY
        }
        // Android 14+ lets the user swipe the notification away; the work goes
        // on, so show it again at once.
        if (intent?.action == ACTION_SHOW) {
            transcriber.state.value?.let { current ->
                val waiting = transcriber.queue.value.count { it.error == null && it.key != current.key }
                Notifications.update(this, current, waiting)
            }
            return START_NOT_STICKY
        }
        // A service started with startForegroundService() must go to the
        // foreground even if the job has already ended.
        val state = transcriber.state.value
        // Not ServiceCompat: it drops the types newer than Android 14, such as
        // mediaProcessing, and Android then refuses a service of type none.
        startForeground(
            Notifications.PROGRESS_ID,
            if (state != null) Notifications.progress(this, state) else Notifications.idle(this),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            },
        )
        if (wakeLock == null) {
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "sillok:transcription")
                .apply { acquire(WAKE_LOCK_LIMIT_MS) }
        }
        if (watcher == null) {
            watcher = scope.launch {
                // Collecting a StateFlow keeps only the latest state, so the
                // delay limits the notification to one update a second.
                transcriber.state.collect { current ->
                    if (current == null) {
                        stopForeground(STOP_FOREGROUND_REMOVE)
                        stopSelf()
                    } else {
                        val waiting = transcriber.queue.value.count { it.error == null && it.key != current.key }
                        Notifications.update(this@TranscriptionService, current, waiting)
                        delay(1000)
                    }
                }
            }
        }
        return START_NOT_STICKY
    }

    // From Android 15, mediaProcessing services may run 6 hours in 24 while
    // the app is in the background (opening the app resets it). Once the
    // system says so, the service must stop within seconds or the app is
    // ended: stop now rather than when the job has wound down, which can take
    // longer on a GPU.
    override fun onTimeout(startId: Int, fgsType: Int) {
        transcriber.cancel(timeout = true)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        scope.cancel()
        wakeLock?.takeIf { it.isHeld }?.release()
        super.onDestroy()
    }

    companion object {
        const val ACTION_CANCEL = "io.github.linkedlatch.sillok.action.CANCEL"
        const val ACTION_SHOW = "io.github.linkedlatch.sillok.action.SHOW"

        /** Longer than any recording the app expects; the job releases it earlier. */
        private const val WAKE_LOCK_LIMIT_MS = 12 * 60 * 60 * 1000L

        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, TranscriptionService::class.java))
        }
    }
}
