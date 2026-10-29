package io.github.linkedlatch.sillok.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.LocalActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.JobEntry
import io.github.linkedlatch.sillok.service.Notifications.label

@Composable
fun SillokApp(vm: AppViewModel = viewModel()) {
    val activity = LocalActivity.current
    BackHandler(enabled = vm.screen != Screen.Home) {
        if (!vm.back()) activity?.finish()
    }
    when (val models = vm.models) {
        ModelState.Ready -> when (val screen = vm.screen) {
            Screen.Home -> HomeScreen(vm)
            is Screen.Setup -> SetupScreen(vm, screen)
            Screen.Running -> vm.run?.let { RunningScreen(it, vm::cancelRun, { vm.back() }) }
            is Screen.Result -> ResultScreen(vm, screen.id)
        }
        else -> ModelsScreen(models, vm::startDownload, vm::stopDownload)
    }
    vm.error?.let { message ->
        AlertDialog(
            onDismissRequest = vm::dismissError,
            confirmButton = { TextButton(onClick = vm::dismissError) { Text(stringResource(R.string.ok)) } },
            text = { Text(message) },
        )
    }
}

/** Content column that stays readable on tablets and wide windows. */
@Composable
fun CenteredColumn(padding: PaddingValues, content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize().padding(padding), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) { content() }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ModelsScreen(state: ModelState, onDownload: () -> Unit, onStop: () -> Unit) {
    val context = LocalContext.current
    Scaffold(topBar = { TopAppBar(title = { Text(stringResource(R.string.models_title)) }) }) { padding ->
        CenteredColumn(padding) {
            when (state) {
                ModelState.Checking, ModelState.Verifying -> {
                    Text(stringResource(if (state == ModelState.Checking) R.string.models_checking else R.string.models_verifying))
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
                is ModelState.Missing -> {
                    Text(stringResource(R.string.models_body, formatBytes(context, state.total)))
                    state.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                    if (state.downloaded > 0) {
                        LinearProgressIndicator({ state.downloaded.toFloat() / state.total }, Modifier.fillMaxWidth())
                    }
                    Button(onClick = onDownload) {
                        Text(stringResource(if (state.downloaded > 0) R.string.models_resume else R.string.models_download))
                    }
                }
                is ModelState.Downloading -> {
                    Text(stringResource(R.string.models_body, formatBytes(context, state.total)))
                    LinearProgressIndicator({ state.downloaded.toFloat() / state.total }, Modifier.fillMaxWidth())
                    Text(
                        stringResource(R.string.models_progress, formatBytes(context, state.downloaded),
                            formatBytes(context, state.total), formatBytes(context, state.bytesPerSecond)),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    OutlinedButton(onClick = onStop) { Text(stringResource(R.string.models_stop)) }
                }
                ModelState.Ready -> Unit
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HomeScreen(vm: AppViewModel) {
    val context = LocalContext.current
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.open(uri)
    }
    var menu by remember { mutableStateOf(false) }
    var about by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf<JobEntry?>(null) }
    var deleting by remember { mutableStateOf<JobEntry?>(null) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    Box {
                        IconButton(onClick = { menu = true }) {
                            Icon(Icons.Default.MoreVert, stringResource(R.string.more))
                        }
                        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.menu_verify_models)) },
                                onClick = { menu = false; vm.verifyModels() },
                            )
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.menu_about)) },
                                onClick = { menu = false; about = true },
                            )
                        }
                    }
                },
            )
        },
        floatingActionButton = {
            // One job at a time; the next file waits until it ends.
            if (vm.run == null) ExtendedFloatingActionButton(
                onClick = { picker.launch(arrayOf("audio/*", "video/*")) },
                icon = { Icon(Icons.Default.Add, null) },
                text = { Text(stringResource(R.string.home_open)) },
            )
        },
    ) { padding ->
        val run = vm.run
        if (vm.history.isEmpty() && run == null) {
            Box(Modifier.fillMaxSize().padding(padding).padding(32.dp), contentAlignment = Alignment.Center) {
                Text(stringResource(R.string.home_empty), textAlign = TextAlign.Center)
            }
        } else {
            LazyColumn(
                Modifier.fillMaxSize(),
                contentPadding = PaddingValues(
                    top = padding.calculateTopPadding(),
                    bottom = padding.calculateBottomPadding() + 88.dp,  // room for the button
                ),
            ) {
                if (run != null) {
                    item(key = "running") {
                        ListItem(
                            headlineContent = { Text(run.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                            supportingContent = {
                                Text(stringResource(R.string.notification_progress, stringResource(run.step.label), (run.fraction * 100).toInt()))
                            },
                            leadingContent = { CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp) },
                            colors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
                            modifier = Modifier.clickable(onClick = vm::showRunning),
                        )
                    }
                }
                items(vm.history, key = { it.id }) { entry ->
                    HistoryItem(
                        entry,
                        supporting = formatDate(context, entry.createdMs) + " · " + formatTime(entry.durationMs),
                        onOpen = { vm.show(entry.id) },
                        onRename = { renaming = entry },
                        onDelete = { deleting = entry },
                    )
                }
            }
        }
    }

    renaming?.let { entry ->
        RenameDialog(entry.title, onDismiss = { renaming = null }) {
            vm.rename(entry, it)
            renaming = null
        }
    }
    deleting?.let { entry ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            text = { Text(stringResource(R.string.delete_confirm, entry.title)) },
            confirmButton = {
                TextButton(onClick = { vm.delete(entry.id); deleting = null }) { Text(stringResource(R.string.delete)) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    if (about) {
        AlertDialog(
            onDismissRequest = { about = false },
            title = { Text(stringResource(R.string.app_name)) },
            text = { Text(vm.version) },
            confirmButton = { TextButton(onClick = { about = false }) { Text(stringResource(R.string.ok)) } },
        )
    }
}

@Composable
private fun HistoryItem(
    entry: JobEntry,
    supporting: String,
    onOpen: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    ListItem(
        headlineContent = { Text(entry.title, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        supportingContent = { Text(supporting) },
        trailingContent = {
            Box {
                IconButton(onClick = { menu = true }) { Icon(Icons.Default.MoreVert, stringResource(R.string.more)) }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text(stringResource(R.string.rename)) }, onClick = { menu = false; onRename() })
                    DropdownMenuItem(text = { Text(stringResource(R.string.delete)) }, onClick = { menu = false; onDelete() })
                }
            }
        },
        modifier = Modifier.clickable(onClick = onOpen),
    )
}

@Composable
fun RenameDialog(current: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
    var text by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.rename)) },
        text = {
            OutlinedTextField(text, { text = it }, label = { Text(stringResource(R.string.title)) }, singleLine = true)
        },
        confirmButton = { TextButton(onClick = { onSave(text) }) { Text(stringResource(R.string.save)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
fun LabeledRow(label: String, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
        content()
    }
}
