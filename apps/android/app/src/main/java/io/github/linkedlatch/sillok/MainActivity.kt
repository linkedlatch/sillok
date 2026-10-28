package io.github.linkedlatch.sillok

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import io.github.linkedlatch.sillok.ui.SillokApp
import io.github.linkedlatch.sillok.ui.SillokTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            SillokTheme {
                SillokApp()
            }
        }
    }
}
