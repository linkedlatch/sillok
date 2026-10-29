package io.github.linkedlatch.sillok

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import io.github.linkedlatch.sillok.service.Notifications
import io.github.linkedlatch.sillok.ui.AppViewModel
import io.github.linkedlatch.sillok.ui.SillokApp
import io.github.linkedlatch.sillok.ui.SillokTheme

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (savedInstanceState == null) openFrom(intent)
        setContent {
            SillokTheme {
                SillokApp(vm)
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        openFrom(intent)
    }

    /** A notification about a finished job opens its result. */
    private fun openFrom(intent: Intent) {
        intent.getStringExtra(Notifications.EXTRA_JOB_ID)?.let(vm::requestOpen)
    }
}
