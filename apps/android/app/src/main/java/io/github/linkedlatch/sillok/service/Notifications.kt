package io.github.linkedlatch.sillok.service

import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.annotation.ChecksSdkIntAtLeast
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ProcessLifecycleOwner
import io.github.linkedlatch.sillok.MainActivity
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.RunOutcome
import io.github.linkedlatch.sillok.data.RunState
import io.github.linkedlatch.sillok.data.Step

object Notifications {
    const val PROGRESS_ID = 1
    private const val FINISHED_ID = 2
    private const val PROGRESS_CHANNEL = "progress"
    private const val FINISHED_CHANNEL = "finished"
    const val EXTRA_JOB_ID = "io.github.linkedlatch.sillok.extra.JOB_ID"

    fun createChannels(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(PROGRESS_CHANNEL, context.getString(R.string.channel_progress), NotificationManager.IMPORTANCE_LOW),
        )
        manager.createNotificationChannel(
            NotificationChannel(FINISHED_CHANNEL, context.getString(R.string.channel_finished), NotificationManager.IMPORTANCE_DEFAULT),
        )
    }

    fun idle(context: Context) = NotificationCompat.Builder(context, PROGRESS_CHANNEL)
        .setSmallIcon(R.drawable.ic_notification)
        .setContentTitle(context.getString(R.string.running_title))
        .build()

    fun progress(context: Context, state: RunState) = NotificationCompat.Builder(context, PROGRESS_CHANNEL)
        .setSmallIcon(R.drawable.ic_notification)
        .setContentTitle(state.name)
        .setContentText(
            if (state.cancelling) context.getString(R.string.running_cancelling)
            else context.getString(R.string.notification_progress, context.getString(state.step.label), (state.fraction * 100).toInt()),
        )
        .setProgress(1000, (state.fraction * 1000).toInt(), false)
        .setOngoing(true)
        .setOnlyAlertOnce(true)
        .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
        .setContentIntent(openApp(context, null))
        .addAction(
            0, context.getString(R.string.cancel),
            PendingIntent.getService(
                context, 0,
                Intent(context, TranscriptionService::class.java).setAction(TranscriptionService.ACTION_CANCEL),
                PendingIntent.FLAG_IMMUTABLE,
            ),
        )
        .build()

    @SuppressLint("MissingPermission")  // canNotify checks it
    fun update(context: Context, state: RunState) {
        if (!canNotify(context)) return
        NotificationManagerCompat.from(context).notify(PROGRESS_ID, progress(context, state))
    }

    /** Tells a user who left the app how the job ended; the app shows it itself otherwise. */
    @SuppressLint("MissingPermission")  // canNotify checks it
    fun finished(context: Context, name: String, outcome: RunOutcome) {
        val inForeground = ProcessLifecycleOwner.get().lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)
        if (inForeground || outcome == RunOutcome.Cancelled || !canNotify(context)) return
        val (text, id) = when (outcome) {
            is RunOutcome.Done -> context.getString(R.string.notification_done) to outcome.id
            is RunOutcome.Failed -> outcome.message to null
            RunOutcome.Cancelled -> return
        }
        val notification = NotificationCompat.Builder(context, FINISHED_CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(name)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setAutoCancel(true)
            .setContentIntent(openApp(context, id))
            .build()
        NotificationManagerCompat.from(context).notify(FINISHED_ID, notification)
    }

    private fun openApp(context: Context, jobId: String?): PendingIntent {
        val intent = Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            .putExtra(EXTRA_JOB_ID, jobId)
        return PendingIntent.getActivity(
            context, jobId?.hashCode() ?: 0, intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
    }

    // Before Android 13 posting needs no permission.
    @ChecksSdkIntAtLeast(api = Build.VERSION_CODES.TIRAMISU)
    private fun needsPermission() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

    private fun canNotify(context: Context) =
        !needsPermission() ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    val Step.label
        get() = when (this) {
            Step.READ -> R.string.step_read
            Step.ANALYZE -> R.string.step_analyze
            Step.TRANSCRIBE -> R.string.step_transcribe
            Step.DIARIZE -> R.string.step_diarize
        }
}
