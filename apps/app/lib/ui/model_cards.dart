// The transcription models with their versions, to download, use or delete:
// in the settings and on the first start.
import 'package:flutter/material.dart';

import '../app_state.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'home.dart';

/// A card for each transcription model, in catalog order.
class ModelCards extends StatelessWidget {
  const ModelCards({super.key, required this.state, this.onChanged});

  final AppState state;

  /// After a model was deleted.
  final VoidCallback? onChanged;

  @override
  Widget build(BuildContext context) {
    final families = <String, List<CatalogModel>>{};
    for (final model in state.transcriptionModels) {
      families.putIfAbsent(model.family, () => []).add(model);
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (final versions in families.values) _ModelCard(state: state, versions: versions, onChanged: onChanged),
      ],
    );
  }
}

/// A model family, with a choice among its versions (quantizations) and what
/// can be done with the chosen one.
class _ModelCard extends StatefulWidget {
  const _ModelCard({required this.state, required this.versions, required this.onChanged});

  final AppState state;
  final List<CatalogModel> versions;
  final VoidCallback? onChanged;

  @override
  State<_ModelCard> createState() => _ModelCardState();
}

class _ModelCardState extends State<_ModelCard> {
  String? _chosen;

  /// The version picked here, else the one in use, a downloaded one or the
  /// recommended one.
  CatalogModel get _model {
    final state = widget.state;
    final versions = widget.versions;
    final inUse = state.defaults.transcriptionModel ?? state.recommendedTranscription.id;
    return versions.where((m) => m.id == _chosen).firstOrNull ??
        versions.where((m) => m.id == inUse).firstOrNull ??
        versions.where((m) => state.models[m.id] is ModelReady).firstOrNull ??
        versions.where((m) => m.recommended).firstOrNull ??
        versions.first;
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final state = widget.state;
    final model = _model;
    final ready = state.models[model.id] is ModelReady;
    final inUse = (state.defaults.transcriptionModel ?? state.recommendedTranscription.id) == model.id;
    // Apple builds offer the NPU encoder (Neural Engine) for a model that is here.
    final accelerator = ready ? state.accelerators[model.id] : null;
    return Card.outlined(
      margin: const EdgeInsets.symmetric(vertical: 4),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          spacing: 8,
          children: [
            Row(
              children: [
                Expanded(child: Text(familyName(model.family), style: Theme.of(context).textTheme.titleMedium)),
                if (widget.versions.any((m) => m.recommended))
                  Chip(label: Text(l.modelRecommended), visualDensity: VisualDensity.compact),
              ],
            ),
            // Downloaded versions carry a mark.
            SegmentedButton<String>(
              showSelectedIcon: false,
              segments: [
                for (final version in widget.versions)
                  ButtonSegment(
                    value: version.id,
                    label: Text(version.quantization),
                    icon: state.models[version.id] is ModelReady ? const Icon(Icons.download_done) : null,
                  ),
              ],
              selected: {model.id},
              onSelectionChanged: (ids) => setState(() => _chosen = ids.first),
            ),
            Text(modelSummary(l, model), style: Theme.of(context).textTheme.bodySmall),
            if (accelerator != null) _AcceleratorControl(state: state, model: model, status: accelerator),
            if (ready)
              Wrap(
                spacing: 8,
                children: [
                  inUse
                      ? FilledButton.tonal(onPressed: null, child: Text(l.modelInUse))
                      : OutlinedButton(
                          onPressed: () =>
                              state.setDefaults(state.defaults.copyWith(transcriptionModel: () => model.id)),
                          child: Text(l.modelUse),
                        ),
                  TextButton(
                    onPressed: () async {
                      if (await confirm(context, l.deleteConfirm(modelName(model)), l.delete)) {
                        await state.deleteModel(model.id);
                        widget.onChanged?.call();
                      }
                    },
                    child: Text(l.delete),
                  ),
                ],
              )
            else
              ModelDownloadControl(state: state, model: model),
          ],
        ),
      ),
    );
  }
}

/// The NPU encoder of a ready model: in use, being fetched, or
/// the button to fetch it.
class _AcceleratorControl extends StatelessWidget {
  const _AcceleratorControl({required this.state, required this.model, required this.status});

  final AppState state;
  final CatalogModel model;
  final ModelStatus status;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final style = Theme.of(context).textTheme.bodySmall;
    final size = model.files.where((f) => f.zip).fold(0, (sum, f) => sum + f.size);
    return switch (status) {
      ModelReady() => Row(
        spacing: 4,
        children: [
          const Icon(Icons.memory, size: 16),
          Text(l.npuReady, style: style),
        ],
      ),
      ModelDownloading(:final progress) => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 4,
        children: [
          LinearProgressIndicator(value: progress.total > 0 ? progress.done / progress.total : null),
          Text(
            l.modelsProgress(
              formatBytes(progress.done),
              formatBytes(progress.total),
              formatBytes(progress.bytesPerSecond),
            ),
            style: style,
          ),
          TextButton(onPressed: state.stopDownload, child: Text(l.modelsStop)),
        ],
      ),
      ModelVerifying() => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 4,
        children: [
          const LinearProgressIndicator(),
          Text(l.npuUnpacking, style: style),
        ],
      ),
      _ => Align(
        alignment: AlignmentDirectional.centerStart,
        child: TextButton.icon(
          onPressed: () => state.downloadAccelerator(model.id),
          icon: const Icon(Icons.memory),
          label: Text(l.npuDownload(formatBytes(size))),
        ),
      ),
    };
  }
}

/// Download progress of [model], or the button to download it with what else
/// it needs.
class ModelDownloadControl extends StatelessWidget {
  const ModelDownloadControl({super.key, required this.state, required this.model});

  final AppState state;
  final CatalogModel model;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final style = Theme.of(context).textTheme.bodySmall;
    return switch (state.models[model.id]) {
      ModelDownloading(:final progress) => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 8,
        children: [
          LinearProgressIndicator(value: progress.total > 0 ? progress.done / progress.total : null),
          Text(
            l.modelsProgress(
              formatBytes(progress.done),
              formatBytes(progress.total),
              formatBytes(progress.bytesPerSecond),
            ),
            style: style,
          ),
          OutlinedButton(onPressed: state.stopDownload, child: Text(l.modelsStop)),
        ],
      ),
      ModelVerifying() => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 8,
        children: [
          const LinearProgressIndicator(),
          Text(l.modelsVerifying, style: style),
        ],
      ),
      ModelMissing(:final downloaded) => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 8,
        children: [
          FilledButton(
            onPressed: () => state.downloadModel(model.id),
            child: Text(downloaded > 0 ? l.modelsResume : l.modelsDownload(formatBytes(state.downloadSize(model.id)))),
          ),
          // Apple builds: the NPU encoder comes with the model.
          if (state.downloadSize(model.id, accelerator: true) case final size when size > 0)
            Text(l.npuIncluded(formatBytes(size)), style: style),
        ],
      ),
      _ => const SizedBox.shrink(),
    };
  }
}
