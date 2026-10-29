package io.github.linkedlatch.sillok

import android.app.Application
import io.github.linkedlatch.sillok.data.CrashLog
import io.github.linkedlatch.sillok.data.Transcriber
import io.github.linkedlatch.sillok.service.Notifications

class SillokApplication : Application() {
    /** The one place transcriptions run, for every screen and the service. */
    val transcriber by lazy { Transcriber(this) }
    val crashLog by lazy { CrashLog(this) }

    override fun onCreate() {
        super.onCreate()
        crashLog.install()
        Notifications.createChannels(this)
    }
}
