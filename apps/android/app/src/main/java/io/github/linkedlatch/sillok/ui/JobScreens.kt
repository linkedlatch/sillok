package io.github.linkedlatch.sillok.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.SystemClock
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.core.content.ContextCompat
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.RunState
import io.github.linkedlatch.sillok.service.Notifications.label
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SetupScreen(vm: AppViewModel, screen: Screen.Setup) {
    var settings by remember { mutableStateOf(vm.settings) }
    val gpus = remember { vm.devices.filter { it.isGpu } }
    // A remembered GPU that is gone (another build or device) falls back to the CPU.
    if (settings.gpuIndex != null && settings.gpuIndex!! >= gpus.size) settings = settings.copy(gpuIndex = null)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.setup_title)) },
                navigationIcon = {
                    IconButton(onClick = { vm.back() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
            )
        },
    ) { padding ->
        Column(Modifier.verticalScroll(rememberScrollState())) {
            CenteredColumn(padding) {
                Text(screen.name, style = MaterialTheme.typography.titleMedium)

                Text(stringResource(R.string.setup_language), style = MaterialTheme.typography.labelLarge)
                val languages = listOf(
                    "auto" to R.string.language_auto,
                    "ko" to R.string.language_ko,
                    "en" to R.string.language_en,
                )
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                    languages.forEachIndexed { i, (code, label) ->
                        SegmentedButton(
                            selected = settings.language == code,
                            onClick = { settings = settings.copy(language = code) },
                            shape = SegmentedButtonDefaults.itemShape(i, languages.size),
                        ) { Text(stringResource(label)) }
                    }
                }

                LabeledRow(stringResource(R.string.setup_diarize)) {
                    Switch(settings.diarize, { settings = settings.copy(diarize = it) })
                }

                OutlinedTextField(
                    settings.glossary,
                    { settings = settings.copy(glossary = it) },
                    Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.setup_glossary)) },
                    supportingText = { Text(stringResource(R.string.setup_glossary_hint)) },
                    minLines = 3,
                )

                if (gpus.isNotEmpty()) {
                    Text(stringResource(R.string.setup_device), style = MaterialTheme.typography.labelLarge)
                    Column(Modifier.selectableGroup()) {
                        val choices = listOf<Pair<Int?, String>>(null to stringResource(R.string.device_cpu)) +
                            gpus.mapIndexed { i, gpu -> i to gpu.description }
                        choices.forEach { (index, label) ->
                            Row(
                                Modifier.fillMaxWidth().selectable(
                                    selected = settings.gpuIndex == index,
                                    onClick = { settings = settings.copy(gpuIndex = index) },
                                    role = Role.RadioButton,
                                ),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                RadioButton(selected = settings.gpuIndex == index, onClick = null)
                                Text(label, Modifier.padding(start = 16.dp))
                            }
                        }
                    }
                }

                // The progress notification needs permission from Android 13 on;
                // the job runs either way.
                val askNotifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
                    vm.start(screen.uri, screen.name, settings)
                }
                val context = LocalContext.current
                Button(
                    onClick = {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
                            PackageManager.PERMISSION_GRANTED
                        ) {
                            askNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
                        } else {
                            vm.start(screen.uri, screen.name, settings)
                        }
                    },
                    Modifier.fillMaxWidth(),
                ) {
                    Text(stringResource(R.string.setup_start))
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RunningScreen(run: RunState, onCancel: () -> Unit, onBack: () -> Unit) {
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(1000)
            now = SystemClock.elapsedRealtime()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.running_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
            )
        },
    ) { padding ->
        CenteredColumn(padding) {
            Text(run.name, style = MaterialTheme.typography.titleMedium)
            val current = run.steps.indexOf(run.step)
            run.steps.forEachIndexed { i, step ->
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        when {
                            i < current -> Icon(Icons.Default.Check, null, Modifier.size(24.dp))
                            i == current && !run.cancelling -> CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                            else -> Spacer(Modifier.size(24.dp))
                        }
                        Text(stringResource(step.label), Modifier.padding(start = 16.dp))
                    }
                    if (i == current) {
                        LinearProgressIndicator({ run.fraction.toFloat() }, Modifier.fillMaxWidth())
                        remaining(run, now)?.let {
                            Text(
                                stringResource(R.string.running_remaining, formatTime(it)),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                }
            }
            Text(stringResource(R.string.running_background), style = MaterialTheme.typography.bodySmall)
            OutlinedButton(onClick = onCancel, enabled = !run.cancelling) {
                Text(stringResource(if (run.cancelling) R.string.running_cancelling else R.string.cancel))
            }
        }
    }
}

/** Time left in the current step, extrapolated once it has run long enough to tell. */
private fun remaining(run: RunState, now: Long): Long? {
    val elapsed = now - run.stepStartedAt
    if (run.cancelling || run.fraction < 0.02 || elapsed < 5_000) return null
    return (elapsed * (1 - run.fraction) / run.fraction).toLong()
}
