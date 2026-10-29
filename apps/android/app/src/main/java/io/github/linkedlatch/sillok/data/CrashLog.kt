package io.github.linkedlatch.sillok.data

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/**
 * Keeps the cause of the last crash so the app can show it on the next start:
 * a Kotlin/Java exception's stack trace, or what Android recorded about a
 * native crash or a kill. The user can pass it on to fix the problem.
 */
class CrashLog(private val context: Context) {
    private val file = File(context.filesDir, "last-crash.txt")
    private val prefs = context.getSharedPreferences("crash", Context.MODE_PRIVATE)

    /** Records uncaught exceptions, then lets the system end the process as usual. */
    fun install() {
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            runCatching {
                val trace = StringWriter().also { error.printStackTrace(PrintWriter(it)) }
                file.writeText("thread ${thread.name}\n$trace")
            }
            previous?.uncaughtException(thread, error)
        }
    }

    /** The cause of an abnormal end since the last call, or null. */
    fun takeReport(): String? {
        val parts = mutableListOf<String>()
        if (file.exists()) {
            parts += file.readText()
            file.delete()
        }
        val last = prefs.getLong("seen", 0L)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val exits = context.getSystemService(ActivityManager::class.java)
                .getHistoricalProcessExitReasons(context.packageName, 0, 5)
            exits.firstOrNull()?.let { prefs.edit().putLong("seen", it.timestamp).apply() }
            exits.filter { it.timestamp > last && it.reason in ABNORMAL }.forEach { exit ->
                parts += "exit: ${reasonName(exit.reason)} (status ${exit.status}) ${exit.description ?: ""}"
            }
        }
        return parts.takeIf { it.isNotEmpty() }?.joinToString("\n\n")
    }

    private companion object {
        val ABNORMAL = setOf(
            ApplicationExitInfo.REASON_CRASH,
            ApplicationExitInfo.REASON_CRASH_NATIVE,
            ApplicationExitInfo.REASON_ANR,
            ApplicationExitInfo.REASON_LOW_MEMORY,
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE,
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE,
        )

        fun reasonName(reason: Int) = when (reason) {
            ApplicationExitInfo.REASON_CRASH -> "crash"
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "native crash"
            ApplicationExitInfo.REASON_ANR -> "not responding (ANR)"
            ApplicationExitInfo.REASON_LOW_MEMORY -> "low memory"
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "excessive resource use"
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "initialization failure"
            else -> "reason $reason"
        }
    }
}
