package io.github.linkedlatch.sillok.ui

import android.content.Intent
import android.media.MediaPlayer
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.BottomAppBar
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.data.JobEntry
import io.github.linkedlatch.sillok.data.Step
import io.github.linkedlatch.sillok.engine.Transcript
import io.github.linkedlatch.sillok.service.Notifications.label
import java.io.File
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

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

    fun playSegment(startMs: Long) {
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ResultScreen(vm: AppViewModel, id: String) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val entry = vm.entry(id) ?: return
    var transcript by remember(id) { mutableStateOf<Transcript?>(null) }
    var loaded by remember(id) { mutableStateOf(false) }
    LaunchedEffect(id) {
        transcript = vm.loadTranscript(id)
        loaded = true
    }

    val player = remember(id) { SegmentPlayer(vm.audioFile(id)) }
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

    var shareMenu by remember { mutableStateOf(false) }
    var info by remember { mutableStateOf(false) }
    var naming by remember { mutableStateOf(false) }
    val speakerLabel = stringResource(R.string.speaker_label)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(entry.title, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                navigationIcon = {
                    IconButton(onClick = { vm.back() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
                actions = {
                    val current = transcript ?: return@TopAppBar
                    IconButton(onClick = { info = true }) {
                        Icon(Icons.Default.Info, stringResource(R.string.info))
                    }
                    if (current.speakers.isNotEmpty()) {
                        IconButton(onClick = { naming = true }) {
                            Icon(Icons.Default.Person, stringResource(R.string.speakers))
                        }
                    }
                    Box {
                        IconButton(onClick = { shareMenu = true }) {
                            Icon(Icons.Default.Share, stringResource(R.string.share))
                        }
                        DropdownMenu(expanded = shareMenu, onDismissRequest = { shareMenu = false }) {
                            listOf(
                                "txt" to R.string.export_txt,
                                "srt" to R.string.export_srt,
                                "json" to R.string.export_json,
                            ).forEach { (format, label) ->
                                DropdownMenuItem(text = { Text(stringResource(label)) }, onClick = {
                                    shareMenu = false
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
                                })
                            }
                        }
                    }
                },
            )
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
                        modifier = Modifier.weight(1f).padding(horizontal = 12.dp),
                    )
                    Text(formatTime(player.durationMs), Modifier.padding(end = 16.dp), style = MaterialTheme.typography.labelMedium)
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
                val playingIndex = if (playing || position > 0) current.segmentAt(position) else -1
                val list = rememberLazyListState()
                LaunchedEffect(playingIndex) {
                    if (playing && playingIndex >= 0 && list.layoutInfo.visibleItemsInfo.none { it.index == playingIndex }) {
                        list.animateScrollToItem(playingIndex)
                    }
                }
                LazyColumn(Modifier.fillMaxSize(), state = list, contentPadding = padding) {
                    itemsIndexed(current.segments) { i, segment ->
                        val speaker = segment.speaker?.let {
                            current.speakerName(it) ?: speakerLabel.replace("{n}", (it + 1).toString())
                        }
                        ListItem(
                            overlineContent = {
                                Text(listOfNotNull(speaker, formatTime(segment.startMs)).joinToString(" · "))
                            },
                            headlineContent = { Text(segment.text) },
                            colors = if (i == playingIndex) {
                                ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
                            } else {
                                ListItemDefaults.colors()
                            },
                            modifier = Modifier.clickable(enabled = player.available) { player.playSegment(segment.startMs) },
                        )
                    }
                }
            }
        }
    }

    val current = transcript
    if (info && current != null) {
        RunDetailsDialog(entry, current, onDismiss = { info = false })
    }
    if (naming && current != null) {
        SpeakerDialog(current, speakerLabel, onDismiss = { naming = false }) { names ->
            naming = false
            scope.launch { transcript = vm.saveSpeakerNames(id, current, names) }
        }
    }
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
            val device = if (model.fellBackToCpu) stringResource(R.string.info_fell_back, model.device) else model.device
            add(stringResource(R.string.info_device, device))
        }
        entry.stats?.let { add(stringResource(R.string.info_threads, it.threads)) }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.info)) },
        text = { Text(lines.joinToString("\n")) },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.ok)) } },
    )
}

@Composable
private fun Message(text: String, padding: androidx.compose.foundation.layout.PaddingValues) {
    Box(Modifier.fillMaxSize().padding(padding).padding(32.dp), contentAlignment = Alignment.Center) {
        Text(text)
    }
}

@Composable
private fun SpeakerDialog(
    transcript: Transcript,
    label: String,
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
                    OutlinedTextField(
                        names[speaker.id] ?: "",
                        { names[speaker.id] = it },
                        Modifier.fillMaxWidth().padding(vertical = 4.dp),
                        label = { Text(default) },
                        placeholder = { Text(default) },
                        singleLine = true,
                    )
                }
            }
        },
        confirmButton = { TextButton(onClick = { onSave(names.toMap()) }) { Text(stringResource(R.string.save)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}
