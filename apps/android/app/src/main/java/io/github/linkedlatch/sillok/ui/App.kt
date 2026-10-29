package io.github.linkedlatch.sillok.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.BackHandler
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.adaptive.ExperimentalMaterial3AdaptiveApi
import androidx.compose.material3.adaptive.layout.AnimatedPane
import androidx.compose.material3.adaptive.layout.ListDetailPaneScaffoldRole
import androidx.compose.material3.adaptive.navigation.NavigableListDetailPaneScaffold
import androidx.compose.material3.adaptive.navigation.rememberListDetailPaneScaffoldNavigator
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.JobSettings
import io.github.linkedlatch.sillok.engine.CatalogModel
import io.github.linkedlatch.sillok.service.Notifications.label
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun SillokApp(vm: AppViewModel = viewModel()) {
    when {
        !vm.modelsChecked -> Box(Modifier.fillMaxSize())
        // First start: nothing to transcribe with yet.
        vm.readyTranscriptionModels().isEmpty() && vm.library.isEmpty() -> WelcomeScreen(vm)
        vm.showSettings -> {
            BackHandler { vm.openSettings(false) }
            SettingsScreen(vm)
        }
        else -> MainScreen(vm)
    }
    vm.pending?.let { AddSheet(vm) }
    vm.message?.let { message ->
        AlertDialog(
            onDismissRequest = vm::dismissMessage,
            confirmButton = { TextButton(onClick = vm::dismissMessage) { Text(stringResource(R.string.ok)) } },
            text = { Text(message) },
        )
    }
    vm.crashReport?.let { CrashDialog(vm, it) }
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

@Composable
fun LabeledRow(label: String, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
        content()
    }
}

// --- First start ------------------------------------------------------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun WelcomeScreen(vm: AppViewModel) {
    val context = LocalContext.current
    val recommended = vm.recommendedTranscription()
    val size = vm.catalog.filter { it.recommended }.sumOf { it.size }
    Scaffold(topBar = { TopAppBar(title = { Text(stringResource(R.string.app_name)) }) }) { padding ->
        CenteredColumn(padding) {
            Text(stringResource(R.string.welcome_title), style = MaterialTheme.typography.headlineSmall)
            Text(stringResource(R.string.welcome_body, formatBytes(context, size)))
            ModelDownloadControl(vm, recommended)
        }
    }
}

/** Download progress of [model], or the button to download it. */
@Composable
fun ModelDownloadControl(vm: AppViewModel, model: CatalogModel) {
    val context = LocalContext.current
    when (val status = vm.models[model.id]) {
        is ModelStatus.Downloading -> Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            LinearProgressIndicator({ status.done.toFloat() / status.total.coerceAtLeast(1) }, Modifier.fillMaxWidth())
            Text(
                stringResource(R.string.models_progress, formatBytes(context, status.done),
                    formatBytes(context, status.total), formatBytes(context, status.bytesPerSecond)),
                style = MaterialTheme.typography.bodySmall,
            )
            OutlinedButton(onClick = vm::stopDownload) { Text(stringResource(R.string.models_stop)) }
        }
        ModelStatus.Verifying -> Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            LinearProgressIndicator(Modifier.fillMaxWidth())
            Text(stringResource(R.string.models_verifying), style = MaterialTheme.typography.bodySmall)
        }
        else -> {
            val started = (status as? ModelStatus.Missing)?.downloaded?.let { it > 0 } == true
            Button(onClick = { vm.downloadModel(model.id) }) {
                Text(stringResource(if (started) R.string.models_resume else R.string.models_download))
            }
        }
    }
}

// --- Library and detail ---------------------------------------------------------

@OptIn(ExperimentalMaterial3AdaptiveApi::class)
@Composable
private fun MainScreen(vm: AppViewModel) {
    val navigator = rememberListDetailPaneScaffoldNavigator<String>()
    val scope = rememberCoroutineScope()
    val shown = navigator.currentDestination?.contentKey
    fun open(key: String) {
        scope.launch { navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, key) }
    }
    // A notification about a finished file opens it.
    LaunchedEffect(vm.openRequest) {
        vm.openRequest?.let {
            navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, it)
            vm.requestOpen(null)
        }
    }
    // The running file's page becomes its transcript when it finishes,
    // in its place, so that going back does not return to it.
    LaunchedEffect(vm.finished) {
        val (key, id) = vm.finished ?: return@LaunchedEffect
        if (navigator.currentDestination?.contentKey == key) {
            navigator.navigateBack()
            navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, id)
        }
    }
    // A page whose file is gone (cancelled, removed, deleted) goes back. A
    // finished file leaves the list for a moment before its transcript
    // takes its place, so wait for that first.
    val keys = vm.library.map { it.key }
    LaunchedEffect(shown, keys) {
        if (shown == null || shown in keys) return@LaunchedEffect
        delay(1000)
        if (navigator.currentDestination?.contentKey == shown && vm.library.none { it.key == shown }) navigator.navigateBack()
    }
    NavigableListDetailPaneScaffold(
        navigator = navigator,
        listPane = {
            AnimatedPane { LibraryPane(vm, shown, ::open) }
        },
        detailPane = {
            AnimatedPane {
                if (shown == null) {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Text(stringResource(R.string.detail_none), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                } else {
                    DetailPane(vm, shown, showBack = navigator.canNavigateBack()) {
                        scope.launch { navigator.navigateBack() }
                    }
                }
            }
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LibraryPane(vm: AppViewModel, selected: String?, onOpen: (String) -> Unit) {
    val context = LocalContext.current
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { vm.pick(it) }
    var now by remember { mutableLongStateOf(android.os.SystemClock.elapsedRealtime()) }
    LaunchedEffect(vm.run != null) {
        while (vm.run != null) {
            now = android.os.SystemClock.elapsedRealtime()
            delay(1000)
        }
    }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    IconButton(onClick = { vm.openSettings(true) }) {
                        Icon(Icons.Default.Settings, stringResource(R.string.settings))
                    }
                },
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { picker.launch(arrayOf("audio/*", "video/*")) },
                icon = { Icon(Icons.Default.Add, null) },
                text = { Text(stringResource(R.string.add)) },
            )
        },
    ) { padding ->
        val items = vm.library
        if (items.isEmpty()) {
            Box(Modifier.fillMaxSize().padding(padding).padding(32.dp), contentAlignment = Alignment.Center) {
                Text(stringResource(R.string.home_empty), textAlign = TextAlign.Center)
            }
            return@Scaffold
        }
        LazyColumn(
            Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                top = padding.calculateTopPadding(),
                bottom = padding.calculateBottomPadding() + 88.dp,  // room for the button
            ),
        ) {
            items(items, key = { it.key }) { item ->
                val colors = if (item.key == selected) {
                    ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
                } else {
                    ListItemDefaults.colors()
                }
                val modifier = Modifier.clickable { onOpen(item.key) }
                when (item) {
                    is LibraryItem.Running -> {
                        val run = item.run
                        val percent = (run.overall * 100).toInt()
                        val left = run.remainingMs(now)
                        ListItem(
                            headlineContent = { Text(run.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                            supportingContent = {
                                Text(
                                    if (left != null) stringResource(R.string.library_running_left, stringResource(run.step.label), percent, formatTime(left))
                                    else stringResource(R.string.library_running, stringResource(run.step.label), percent),
                                )
                            },
                            leadingContent = { CircularProgressIndicator({ run.overall.toFloat() }, Modifier.size(28.dp)) },
                            colors = colors,
                            modifier = modifier,
                        )
                    }
                    is LibraryItem.Waiting -> ListItem(
                        headlineContent = { Text(item.item.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        supportingContent = { Text(stringResource(R.string.library_waiting, item.position)) },
                        colors = colors,
                        modifier = modifier,
                    )
                    is LibraryItem.Failed -> ListItem(
                        headlineContent = { Text(item.item.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        supportingContent = {
                            Text(stringResource(R.string.library_failed, item.item.error ?: ""), maxLines = 2,
                                overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.error)
                        },
                        leadingContent = { Icon(Icons.Default.Warning, null, tint = MaterialTheme.colorScheme.error) },
                        colors = colors,
                        modifier = modifier,
                    )
                    is LibraryItem.Done -> ListItem(
                        headlineContent = { Text(item.entry.title, maxLines = 2, overflow = TextOverflow.Ellipsis) },
                        supportingContent = { Text(formatDate(context, item.entry.createdMs) + " · " + formatTime(item.entry.durationMs)) },
                        colors = colors,
                        modifier = modifier,
                    )
                }
            }
        }
    }
}

// --- Adding files -----------------------------------------------------------------

/** The picked files with a summary of the settings; options open only when wanted. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddSheet(vm: AppViewModel) {
    val pending = vm.pending ?: return
    val context = LocalContext.current
    var settings by remember { mutableStateOf(vm.defaults) }
    var options by remember { mutableStateOf(false) }
    val model = settings.transcriptionModel ?: vm.recommendedTranscription().id
    val ready = vm.modelsReady(settings.transcriptionModel)
    // The progress notification needs permission from Android 13 on; the work runs either way.
    val askNotifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { vm.start(settings) }
    ModalBottomSheet(onDismissRequest = vm::cancelPending, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)) {
        Column(
            Modifier.padding(horizontal = 24.dp).padding(bottom = 24.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                if (pending.files.size == 1) stringResource(R.string.add_title_one)
                else pluralStringResource(R.plurals.add_title, pending.files.size, pending.files.size),
                style = MaterialTheme.typography.titleLarge,
            )
            pending.files.take(5).forEach { (_, name) -> Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis) }
            if (pending.files.size > 5) Text("…")
            Text(settingsSummary(vm, settings), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            TextButton(onClick = { options = !options }) { Text(stringResource(R.string.add_options)) }
            if (options) SettingsFields(vm, settings, readyModelsOnly = true) { settings = it }
            if (!ready) {
                Text(stringResource(R.string.add_model_missing, modelName(vm.catalog.first { it.id == model })),
                    color = MaterialTheme.colorScheme.error)
            }
            Button(
                onClick = {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                        ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
                    ) {
                        askNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
                    } else {
                        vm.start(settings)
                    }
                },
                enabled = ready,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(stringResource(R.string.add_start)) }
        }
    }
}

/** "Automatic · speakers told apart · large-v3 turbo / q5_0" */
@Composable
fun settingsSummary(vm: AppViewModel, settings: JobSettings): String {
    val language = stringResource(languageLabel(settings.language))
    val speakers = stringResource(if (settings.diarize) R.string.detail_diarize_on else R.string.detail_diarize_off)
    val model = vm.catalog.firstOrNull { it.id == settings.transcriptionModel } ?: vm.recommendedTranscription()
    return listOf(language, speakers, modelName(model)).joinToString(" · ")
}

fun languageLabel(code: String) = when (code) {
    "ko" -> R.string.language_ko
    "en" -> R.string.language_en
    else -> R.string.language_auto
}

/** "large-v3 turbo / q5_0": the model, then its quantization. */
fun modelName(model: CatalogModel): String = model.family.replace("-turbo", " turbo") + " / " + model.quantization

/** The settings a job runs with: language, speakers, terms, model and device. */
@Composable
fun SettingsFields(vm: AppViewModel, settings: JobSettings, readyModelsOnly: Boolean, onChange: (JobSettings) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.setup_language), style = MaterialTheme.typography.labelLarge)
        val languages = listOf("auto", "ko", "en")
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            languages.forEachIndexed { i, code ->
                SegmentedButton(
                    selected = settings.language == code,
                    onClick = { onChange(settings.copy(language = code)) },
                    shape = SegmentedButtonDefaults.itemShape(i, languages.size),
                ) { Text(stringResource(languageLabel(code))) }
            }
        }
        LabeledRow(stringResource(R.string.setup_diarize)) {
            Switch(settings.diarize, { onChange(settings.copy(diarize = it)) })
        }
        OutlinedTextField(
            settings.glossary,
            { onChange(settings.copy(glossary = it)) },
            Modifier.fillMaxWidth(),
            label = { Text(stringResource(R.string.setup_glossary)) },
            supportingText = { Text(stringResource(R.string.setup_glossary_hint)) },
            minLines = 2,
        )
        if (readyModelsOnly) {
            val models = vm.readyTranscriptionModels()
            if (models.size > 1) {
                Text(stringResource(R.string.setup_model), style = MaterialTheme.typography.labelLarge)
                val current = settings.transcriptionModel ?: vm.recommendedTranscription().id
                Choices(models.map { it.id to modelName(it) }, current) { onChange(settings.copy(transcriptionModel = it)) }
            }
        }
        if (vm.gpus.isNotEmpty()) {
            Text(stringResource(R.string.setup_device), style = MaterialTheme.typography.labelLarge)
            val choices = listOf<Pair<Int?, String>>(null to stringResource(R.string.device_cpu)) +
                vm.gpus.mapIndexed { i, gpu -> i to gpu.description }
            Choices(choices, settings.gpuIndex) { onChange(settings.copy(gpuIndex = it)) }
        }
    }
}

@Composable
private fun <T> Choices(choices: List<Pair<T, String>>, selected: T, onSelect: (T) -> Unit) {
    Column(Modifier.selectableGroup()) {
        choices.forEach { (value, label) ->
            Row(
                Modifier.fillMaxWidth().selectable(selected = value == selected, onClick = { onSelect(value) }, role = Role.RadioButton)
                    .padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RadioButton(selected = value == selected, onClick = null)
                Text(label, Modifier.padding(start = 16.dp))
            }
        }
    }
}

// --- Dialogs --------------------------------------------------------------------------

@Composable
private fun CrashDialog(vm: AppViewModel, report: String) {
    val context = LocalContext.current
    val subject = stringResource(R.string.crash_title)
    AlertDialog(
        onDismissRequest = vm::dismissCrashReport,
        title = { Text(subject) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(stringResource(R.string.crash_body))
                HorizontalDivider(Modifier.padding(vertical = 8.dp))
                SelectionContainer {
                    Text(report, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val send = Intent(Intent.ACTION_SEND).setType("text/plain")
                    .putExtra(Intent.EXTRA_SUBJECT, subject)
                    .putExtra(Intent.EXTRA_TEXT, vm.version + "\n\n" + report)
                context.startActivity(Intent.createChooser(send, subject))
                vm.dismissCrashReport()
            }) { Text(stringResource(R.string.share)) }
        },
        dismissButton = { TextButton(onClick = vm::dismissCrashReport) { Text(stringResource(R.string.ok)) } },
    )
}

@Composable
fun RenameDialog(current: String, onDismiss: () -> Unit, onSave: (String) -> Unit) {
    var text by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.rename)) },
        text = { OutlinedTextField(text, { text = it }, label = { Text(stringResource(R.string.title)) }, singleLine = true) },
        confirmButton = { TextButton(onClick = { onSave(text) }) { Text(stringResource(R.string.save)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}
