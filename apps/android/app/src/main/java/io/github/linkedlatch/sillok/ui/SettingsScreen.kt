package io.github.linkedlatch.sillok.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.linkedlatch.sillok.R
import io.github.linkedlatch.sillok.engine.CatalogModel

/** Defaults for new files, the transcription models, storage and the app's details. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(vm: AppViewModel) {
    val context = LocalContext.current
    var about by remember { mutableStateOf(false) }
    var storage by remember { mutableStateOf<Pair<Long, Long>?>(null) }
    LaunchedEffect(vm.models) { storage = vm.storageUse() }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.settings)) },
                navigationIcon = {
                    IconButton(onClick = { vm.openSettings(false) }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
            )
        },
    ) { padding ->
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
            CenteredColumn(padding) {
                Section(stringResource(R.string.settings_defaults))
                SettingsFields(vm, vm.defaults, readyModelsOnly = false) { vm.saveDefaults(it) }

                HorizontalDivider()
                Section(stringResource(R.string.settings_models))
                Text(stringResource(R.string.settings_models_hint), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                val inUse = vm.defaults.transcriptionModel ?: vm.recommendedTranscription().id
                vm.transcriptionModels().forEach { model ->
                    ModelRow(vm, model, inUse = model.id == inUse) {
                        vm.saveDefaults(vm.defaults.copy(transcriptionModel = model.id.takeUnless { model.recommended }))
                    }
                }

                HorizontalDivider()
                Section(stringResource(R.string.settings_storage))
                storage?.let { (models, history) ->
                    Text(stringResource(R.string.settings_storage_models, formatBytes(context, models)))
                    Text(stringResource(R.string.settings_storage_history, formatBytes(context, history)))
                }

                HorizontalDivider()
                Section(stringResource(R.string.settings_more))
                TextButton(onClick = vm::verifyModels) { Text(stringResource(R.string.menu_verify_models)) }
                TextButton(onClick = { about = true }) { Text(stringResource(R.string.menu_about)) }
            }
        }
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
private fun Section(title: String) {
    Text(title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
}

/** A transcription model: what it is, and whether it is here, downloading or to download. */
@Composable
private fun ModelRow(vm: AppViewModel, model: CatalogModel, inUse: Boolean, onUse: () -> Unit) {
    val context = LocalContext.current
    val status = vm.models[model.id]
    val description = listOf(
        stringResource(familyDescription(model.family)),
        stringResource(quantizationDescription(model.quantization)),
        formatBytes(context, model.size),
    ).joinToString(" · ")
    ListItem(
        headlineContent = {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(modelName(model))
                if (model.recommended) {
                    Text(stringResource(R.string.model_recommended), style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary)
                }
            }
        },
        supportingContent = {
            Column {
                Text(description)
                when (status) {
                    is ModelStatus.Downloading -> Text(stringResource(R.string.model_downloading,
                        formatBytes(context, status.done), formatBytes(context, status.total)))
                    ModelStatus.Verifying -> Text(stringResource(R.string.model_verifying))
                    ModelStatus.Ready -> if (inUse) Text(stringResource(R.string.model_in_use), color = MaterialTheme.colorScheme.primary)
                    else -> Unit
                }
            }
        },
        leadingContent = {
            RadioButton(selected = inUse, onClick = if (status == ModelStatus.Ready) onUse else null, enabled = status == ModelStatus.Ready)
        },
        trailingContent = {
            when (status) {
                is ModelStatus.Downloading -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator({ status.done.toFloat() / status.total.coerceAtLeast(1) }, Modifier.size(24.dp))
                    IconButton(onClick = vm::stopDownload) { Icon(Icons.Default.Close, stringResource(R.string.models_stop)) }
                }
                ModelStatus.Verifying -> CircularProgressIndicator(Modifier.size(24.dp))
                ModelStatus.Ready -> if (!inUse) {
                    IconButton(onClick = { vm.deleteModel(model.id) }) { Icon(Icons.Default.Delete, stringResource(R.string.model_delete)) }
                }
                else -> TextButton(onClick = { vm.downloadModel(model.id) }) { Text(stringResource(R.string.models_download)) }
            }
        },
        modifier = Modifier.clickable(enabled = status == ModelStatus.Ready, onClick = onUse).padding(vertical = 2.dp),
    )
}

private fun familyDescription(family: String) = when (family) {
    "large-v3-turbo" -> R.string.model_family_large_v3_turbo
    "large-v3" -> R.string.model_family_large_v3
    "small" -> R.string.model_family_small
    else -> R.string.model_family_base
}

private fun quantizationDescription(quantization: String) = when {
    quantization == "f16" -> R.string.model_quant_f16
    quantization == "q8_0" -> R.string.model_quant_q8_0
    else -> R.string.model_quant_q5
}
