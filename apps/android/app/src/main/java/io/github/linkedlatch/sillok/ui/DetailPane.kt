package io.github.linkedlatch.sillok.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.media.MediaPlayer
import android.os.SystemClock
import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.BottomAppBar
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.JobEntry
import io.github.linkedlatch.sillok.data.QueueItem
import io.github.linkedlatch.sillok.data.RunState
import io.github.linkedlatch.sillok.data.Step
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.service.Notifications.label
import java.io.File
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** The page of one library item: its progress, its place in line, its failure or its transcript. */
@Composable
fun DetailPane(vm: AppViewModel, key: String, showBack: Boolean, onBack: () -> Unit) {
    when (val item = vm.library.firstOrNull { it.key == key }) {
        is LibraryItem.Running -> RunningDetail(vm, item.run, showBack, onBack)
        is LibraryItem.Waiting -> QueueDetail(vm, item.item, item.position, showBack, onBack)
        is LibraryItem.Failed -> QueueDetail(vm, item.item, null, showBack, onBack)
        is LibraryItem.Done -> TranscriptDetail(vm, item.entry, showBack, onBack)
        null -> Box(Modifier.fillMaxSize())
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DetailTopBar(title: String, showBack: Boolean, onBack: () -> Unit, actions: @Composable () -> Unit = {}) {
    TopAppBar(
        title = { Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        navigationIcon = {
            if (showBack) {
                IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back)) }
            }
        },
        actions = { actions() },
    )
}

// --- Running ----------------------------------------------------------------------

@Composable
private fun RunningDetail(vm: AppViewModel, run: RunState, showBack: Boolean, onBack: () -> Unit) {
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(Unit) {
        while (true) {
            now = SystemClock.elapsedRealtime()
            delay(1000)
        }
    }
    var details by remember { mutableStateOf(false) }
    val list = rememberLazyListState()
    // Follow the newest text as it comes.
    LaunchedEffect(vm.previews.size) { if (vm.previews.isNotEmpty()) list.animateScrollToItem(vm.previews.size + 1) }
    Scaffold(topBar = { DetailTopBar(run.name, showBack, onBack) }) { padding ->
        LazyColumn(Modifier.fillMaxSize(), state = list, contentPadding = padding) {
            item {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    LinearProgressIndicator({ run.overall.toFloat() }, Modifier.fillMaxWidth())
                    Text(stringResource(R.string.running_overall, stringResource(run.step.label), (run.overall * 100).toInt()))
                    run.remainingMs(now)?.let {
                        Text(stringResource(R.string.running_left, formatTime(it)), style = MaterialTheme.typography.bodySmall)
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = vm::cancelRun, enabled = !run.cancelling) {
                            Text(stringResource(if (run.cancelling) R.string.running_cancelling else R.string.cancel))
                        }
                        TextButton(onClick = { details = !details }) { Text(stringResource(R.string.running_details)) }
                    }
                    if (details) {
                        val read = run.read
                        Text(
                            buildString {
                                append(stringResource(R.string.running_background))
                                if (read != null) {
                                    append("\n").append(stringResource(R.string.info_step, stringResource(Step.READ.label), formatTime(read.totalMs)))
                                    append("\n").append(stringResource(R.string.info_read_parts, formatTime(read.decodeMs), formatTime(read.pushMs), formatTime(read.copyMs)))
                                    append("\n").append(stringResource(R.string.info_decoder, read.decoder ?: "?"))
                                }
                            },
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    if (vm.previews.isNotEmpty()) {
                        Text(stringResource(R.string.running_previews), style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
            itemsIndexed(vm.previews) { _, segment ->
                Column(Modifier.padding(horizontal = 16.dp, vertical = 6.dp)) {
                    Text(formatTime(segment.startMs), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(segment.text)
                }
            }
            item { Box(Modifier.padding(16.dp)) }
        }
    }
}

// --- Waiting and failed ----------------------------------------------------------------

@Composable
private fun QueueDetail(vm: AppViewModel, item: QueueItem, position: Int?, showBack: Boolean, onBack: () -> Unit) {
    Scaffold(topBar = { DetailTopBar(item.name, showBack, onBack) }) { padding ->
        CenteredColumn(padding) {
            if (item.error != null) {
                Text(stringResource(R.string.library_failed, item.error), color = MaterialTheme.colorScheme.error)
            } else if (position != null) {
                Text(stringResource(R.string.detail_waiting, position))
            }
            Text(settingsSummary(vm, item.settings), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (item.error != null) Button(onClick = { vm.retry(item.key) }) { Text(stringResource(R.string.retry)) }
                OutlinedButton(onClick = { vm.remove(item.key) }) { Text(stringResource(R.string.remove)) }
            }
        }
    }
}

// --- Transcript ------------------------------------------------------------------------------

/** Plays the listening copy of a job; segments start a second early (include/sillok/sillok.h). */
private class SegmentPlayer(file: File) {
    private val player: MediaPlayer? = runCatching {
        MediaPlayer().apply {
            setDataSource(file.path)
            prepare()
        }
    }.getOrNull()

    val available get() = player != null
    val durationMs get() = player?.duration?.toLong() ?: 0L
    val positionMs get() = player?.currentPosition?.toLong() ?: 0L
    val playing get() = player?.isPlaying == true

    fun playFrom(startMs: Long) {
        seek((startMs - 1000).coerceAtLeast(0))
        player?.start()
    }

    fun seek(ms: Long) {
        player?.seekTo(ms, MediaPlayer.SEEK_CLOSEST)
    }

    fun toggle() {
        val player = player ?: return
        if (player.isPlaying) player.pause() else player.start()
    }

    fun pause() {
        if (playing) player?.pause()
    }

    fun release() {
        player?.release()
    }
}

/** Consecutive segments of one speaker (or, without speakers, close together) read as a paragraph. */
private data class Paragraph(val speaker: Int?, val segments: List<Int>)

private fun paragraphs(transcript: Transcript): List<Paragraph> {
    val result = mutableListOf<Paragraph>()
    var current = mutableListOf<Int>()
    transcript.segments.forEachIndexed { i, segment ->
        val previous = current.lastOrNull()?.let { transcript.segments[it] }
        val breaks = previous != null && (
            previous.speaker != segment.speaker ||
                (segment.speaker == null && segment.startMs - previous.endMs > 2_000) ||
                current.size >= 8
            )
        if (breaks) {
            result += Paragraph(transcript.segments[current.first()].speaker, current)
            current = mutableListOf()
        }
        current += i
    }
    if (current.isNotEmpty()) result += Paragraph(transcript.segments[current.first()].speaker, current)
    return result
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TranscriptDetail(vm: AppViewModel, entry: JobEntry, showBack: Boolean, onBack: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var transcript by remember(entry.id) { mutableStateOf<Transcript?>(null) }
    var loaded by remember(entry.id) { mutableStateOf(false) }
    LaunchedEffect(entry.id) {
        transcript = vm.loadTranscript(entry.id)
        loaded = true
    }
    val player = remember(entry.id) { SegmentPlayer(vm.audioFile(entry.id)) }
    DisposableEffect(player) { onDispose { player.release() } }
    LifecycleEventEffect(Lifecycle.Event.ON_STOP) { player.pause() }
    var position by remember { mutableLongStateOf(0L) }
    var playing by remember { mutableStateOf(false) }
    LaunchedEffect(player) {
        while (true) {
            position = player.positionMs
            playing = player.playing
            delay(100)
        }
    }
    var selected by remember(entry.id) { mutableStateOf<Int?>(null) }
    var follow by remember { mutableStateOf(true) }
    var query by remember { mutableStateOf<String?>(null) }
    var menu by remember { mutableStateOf(false) }
    var naming by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf(false) }
    var info by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf(false) }
    val speakerLabel = stringResource(R.string.speaker_label)

    fun shareRecording() {
        val file = vm.audioFile(entry.id)
        if (!file.exists()) return
        val uri = FileProvider.getUriForFile(context, context.packageName + ".files", file)
        val send = Intent(Intent.ACTION_SEND)
            .setType("audio/mp4")
            .putExtra(Intent.EXTRA_STREAM, uri)
            .putExtra(Intent.EXTRA_SUBJECT, entry.title)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        context.startActivity(Intent.createChooser(send, entry.title))
    }

    fun share(format: String) {
        val current = transcript ?: return
        scope.launch {
            val file = vm.export(entry, current, format)
            val uri = FileProvider.getUriForFile(context, context.packageName + ".files", file)
            val send = Intent(Intent.ACTION_SEND)
                .setType(if (format == "json") "application/json" else "text/plain")
                .putExtra(Intent.EXTRA_STREAM, uri)
                .putExtra(Intent.EXTRA_SUBJECT, entry.title)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            context.startActivity(Intent.createChooser(send, entry.title))
        }
    }

    Scaffold(
        topBar = {
            if (query != null) {
                TopAppBar(
                    title = {
                        TextField(
                            query ?: "", { query = it }, Modifier.fillMaxWidth(),
                            placeholder = { Text(stringResource(R.string.search_hint)) }, singleLine = true,
                            colors = TextFieldDefaults.colors(
                                focusedContainerColor = MaterialTheme.colorScheme.surface,
                                unfocusedContainerColor = MaterialTheme.colorScheme.surface,
                            ),
                        )
                    },
                    navigationIcon = { IconButton(onClick = { query = null }) { Icon(Icons.Default.Close, stringResource(R.string.cancel)) } },
                )
            } else {
                DetailTopBar(entry.title, showBack, onBack) {
                    IconButton(onClick = { query = "" }) { Icon(Icons.Default.Search, stringResource(R.string.search)) }
                    Box {
                        IconButton(onClick = { menu = true }) { Icon(Icons.Default.MoreVert, stringResource(R.string.more)) }
                        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                            if (transcript?.speakers?.isNotEmpty() == true) {
                                DropdownMenuItem(text = { Text(stringResource(R.string.speakers)) }, onClick = { menu = false; naming = true })
                            }
                            DropdownMenuItem(text = { Text(stringResource(R.string.export_txt)) }, onClick = { menu = false; share("txt") })
                            DropdownMenuItem(text = { Text(stringResource(R.string.export_srt)) }, onClick = { menu = false; share("srt") })
                            DropdownMenuItem(text = { Text(stringResource(R.string.export_json)) }, onClick = { menu = false; share("json") })
                            if (player.available) {
                                DropdownMenuItem(text = { Text(stringResource(R.string.export_recording)) }, onClick = { menu = false; shareRecording() })
                            }
                            DropdownMenuItem(text = { Text(stringResource(R.string.rename)) }, onClick = { menu = false; renaming = true })
                            DropdownMenuItem(text = { Text(stringResource(R.string.info)) }, onClick = { menu = false; info = true })
                            DropdownMenuItem(text = { Text(stringResource(R.string.delete)) }, onClick = { menu = false; deleting = true })
                        }
                    }
                }
            }
        },
        bottomBar = {
            if (player.available) {
                BottomAppBar {
                    IconButton(onClick = player::toggle) {
                        if (playing) Icon(painterResource(R.drawable.ic_pause), stringResource(R.string.pause))
                        else Icon(Icons.Default.PlayArrow, stringResource(R.string.play))
                    }
                    Text(formatTime(position), style = MaterialTheme.typography.labelMedium)
                    Slider(
                        value = position.toFloat(),
                        onValueChange = { position = it.toLong(); player.seek(it.toLong()) },
                        valueRange = 0f..player.durationMs.coerceAtLeast(1).toFloat(),
                        modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                    )
                    FilterChip(selected = follow, onClick = { follow = !follow }, label = { Text(stringResource(R.string.follow)) },
                        modifier = Modifier.padding(end = 12.dp))
                }
            }
        },
    ) { padding ->
        val current = transcript
        when {
            !loaded -> Unit
            current == null -> Message(stringResource(R.string.result_missing), padding)
            current.segments.isEmpty() -> Message(stringResource(R.string.result_empty), padding)
            else -> {
                val all = remember(current) { paragraphs(current) }
                val needle = query?.trim().orEmpty()
                val shown = if (needle.isEmpty()) all
                else all.filter { p -> p.segments.any { current.segments[it].text.contains(needle, ignoreCase = true) } }
                val playingIndex = if (playing || position > 0) current.segmentAt(position) else -1
                val list = rememberLazyListState()
                LaunchedEffect(playingIndex, follow) {
                    if (!follow || !playing || playingIndex < 0 || needle.isNotEmpty()) return@LaunchedEffect
                    val paragraph = shown.indexOfFirst { playingIndex in it.segments }
                    if (paragraph >= 0 && list.layoutInfo.visibleItemsInfo.none { it.index == paragraph }) list.animateScrollToItem(paragraph)
                }
                if (shown.isEmpty()) {
                    Message(stringResource(R.string.search_none), padding)
                } else {
                    LazyColumn(Modifier.fillMaxSize(), state = list, contentPadding = padding) {
                        itemsIndexed(shown) { _, paragraph ->
                            ParagraphView(
                                current, paragraph, speakerLabel, playingIndex, selected, needle,
                                onSpeaker = { naming = true },
                                onSegment = { i ->
                                    selected = i
                                    if (player.available) player.playFrom(current.segments[i].startMs)
                                },
                                onPlay = { i -> player.playFrom(current.segments[i].startMs) },
                            )
                        }
                    }
                }
            }
        }
    }

    val current = transcript
    if (naming && current != null) {
        SpeakerDialog(current, speakerLabel, onListen = { id ->
            current.segments.firstOrNull { it.speaker == id }?.let { player.playFrom(it.startMs) }
        }, onDismiss = { naming = false; player.pause() }) { names ->
            naming = false
            scope.launch { transcript = vm.saveSpeakerNames(entry.id, current, names) }
        }
    }
    if (renaming) RenameDialog(entry.title, onDismiss = { renaming = false }) { vm.rename(entry, it); renaming = false }
    if (info && current != null) RunDetailsDialog(entry, current) { info = false }
    if (deleting) {
        AlertDialog(
            onDismissRequest = { deleting = false },
            text = { Text(stringResource(R.string.delete_confirm, entry.title)) },
            confirmButton = { TextButton(onClick = { deleting = false; vm.delete(entry.id); onBack() }) { Text(stringResource(R.string.delete)) } },
            dismissButton = { TextButton(onClick = { deleting = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@Composable
private fun ParagraphView(
    transcript: Transcript,
    paragraph: Paragraph,
    speakerLabel: String,
    playing: Int,
    selected: Int?,
    needle: String,
    onSpeaker: () -> Unit,
    onSegment: (Int) -> Unit,
    onPlay: (Int) -> Unit,
) {
    val context = LocalContext.current
    val colors = MaterialTheme.colorScheme
    val first = transcript.segments[paragraph.segments.first()]
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            paragraph.speaker?.let { id ->
                Text(
                    transcript.speakerName(id) ?: speakerLabel.replace("{n}", (id + 1).toString()),
                    style = MaterialTheme.typography.labelLarge,
                    color = colors.primary,
                    modifier = Modifier.clickable(onClick = onSpeaker),
                )
            }
            Text(formatTime(first.startMs), style = MaterialTheme.typography.labelMedium, color = colors.onSurfaceVariant)
        }
        // Each sentence is a link: tapping it plays it and shows its details.
        val text = buildAnnotatedString {
            paragraph.segments.forEachIndexed { n, i ->
                if (n > 0) append(" ")
                val segment = transcript.segments[i]
                val style = when (i) {
                    playing -> SpanStyle(background = colors.secondaryContainer)
                    selected -> SpanStyle(background = colors.surfaceVariant)
                    else -> SpanStyle()
                }
                withLink(LinkAnnotation.Clickable("segment-$i") { onSegment(i) }) {
                    withStyle(style) { appendHighlighted(segment.text, needle, SpanStyle(background = colors.tertiaryContainer, fontWeight = FontWeight.Bold)) }
                }
            }
        }
        Text(text, style = MaterialTheme.typography.bodyLarge)
        if (selected != null && selected in paragraph.segments) {
            val segment = transcript.segments[selected]
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    val language = when (segment.language) {
                        "ko" -> stringResource(R.string.language_name_ko)
                        "en" -> stringResource(R.string.language_name_en)
                        else -> segment.language
                    }
                    Text(stringResource(R.string.segment_info, preciseTime(segment.startMs), preciseTime(segment.endMs), language),
                        style = MaterialTheme.typography.bodySmall)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        TextButton(onClick = { onPlay(selected) }) { Text(stringResource(R.string.segment_play)) }
                        TextButton(onClick = {
                            context.getSystemService(ClipboardManager::class.java)
                                .setPrimaryClip(ClipData.newPlainText("sillok", segment.text))
                            Toast.makeText(context, R.string.copied, Toast.LENGTH_SHORT).show()
                        }) { Text(stringResource(R.string.segment_copy)) }
                    }
                }
            }
        }
    }
}

private fun AnnotatedString.Builder.appendHighlighted(text: String, needle: String, style: SpanStyle) {
    if (needle.isEmpty()) {
        append(text)
        return
    }
    var start = 0
    while (true) {
        val at = text.indexOf(needle, start, ignoreCase = true)
        if (at < 0) break
        append(text.substring(start, at))
        withStyle(style) { append(text.substring(at, at + needle.length)) }
        start = at + needle.length
    }
    append(text.substring(start))
}

/** "1:02:03.4" or "2:03.4": tenths of a second, for a selected sentence. */
private fun preciseTime(ms: Long): String = formatTime(ms) + "." + (ms % 1000 / 100)

@Composable
private fun Message(text: String, padding: androidx.compose.foundation.layout.PaddingValues) {
    Box(Modifier.fillMaxSize().padding(padding).padding(32.dp), contentAlignment = Alignment.Center) { Text(text) }
}

@Composable
private fun SpeakerDialog(
    transcript: Transcript,
    label: String,
    onListen: (Int) -> Unit,
    onDismiss: () -> Unit,
    onSave: (Map<Int, String>) -> Unit,
) {
    val names = remember { mutableStateMapOf<Int, String>().apply { transcript.speakers.forEach { put(it.id, it.name ?: "") } } }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.speakers)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                transcript.speakers.forEach { speaker ->
                    val default = label.replace("{n}", (speaker.id + 1).toString())
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        OutlinedTextField(
                            names[speaker.id] ?: "", { names[speaker.id] = it }, Modifier.weight(1f).padding(vertical = 4.dp),
                            label = { Text(default) }, placeholder = { Text(default) }, singleLine = true,
                        )
                        IconButton(onClick = { onListen(speaker.id) }) { Icon(Icons.Default.PlayArrow, stringResource(R.string.speaker_listen)) }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = { onSave(names.toMap()) }) { Text(stringResource(R.string.save)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

/** Where the time went, to compare devices and settings. */
@Composable
private fun RunDetailsDialog(entry: JobEntry, transcript: Transcript, onDismiss: () -> Unit) {
    val lines = buildList {
        add(stringResource(R.string.info_length, formatTime(entry.durationMs)))
        entry.stats?.let { stats ->
            var total = 0L
            Step.entries.forEach { step ->
                stats.timingsMs[step.name.lowercase()]?.let {
                    total += it
                    add(stringResource(R.string.info_step, stringResource(step.label), formatTime(it)))
                }
                val decode = stats.timingsMs["read_decode"]
                if (step == Step.READ && decode != null) {
                    add(stringResource(R.string.info_read_parts, formatTime(decode),
                        formatTime(stats.timingsMs["read_push"] ?: 0), formatTime(stats.timingsMs["read_copy"] ?: 0)))
                }
            }
            add(stringResource(R.string.info_total, formatTime(total), total.toDouble() / entry.durationMs.coerceAtLeast(1)))
        }
        transcript.models["transcription"]?.let { model ->
            add(model.name)
            add(stringResource(R.string.info_device, if (model.fellBackToCpu) stringResource(R.string.info_fell_back, model.device) else model.device))
        }
        entry.stats?.let { add(stringResource(R.string.info_threads, it.threads)) }
        entry.stats?.decoder?.let { add(stringResource(R.string.info_decoder, it)) }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.info)) },
        text = { Text(lines.joinToString("\n")) },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.ok)) } },
    )
}
