import 'package:flutter/material.dart';

import '../app_state.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';

/// The first start: what Sillok does, and the recommended models to download.
class WelcomePage extends StatelessWidget {
  const WelcomePage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final size = state.engineModels(null).fold(0, (sum, m) => sum + m.size);
    return Scaffold(
      appBar: AppBar(title: Text(l.appName)),
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) => SafeArea(
          top: false,
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 640),
              child: ListView(
                padding: const EdgeInsets.all(24),
                children: [
                  Text(l.welcomeTitle, style: Theme.of(context).textTheme.headlineSmall),
                  const SizedBox(height: 16),
                  Text(l.welcomeBody(formatBytes(size))),
                  const SizedBox(height: 24),
                  ModelDownloadControl(state: state, model: state.recommendedTranscription),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Download progress of [model], or the button to download it.
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
      ModelMissing(:final downloaded) => Align(
        alignment: AlignmentDirectional.centerStart,
        child: FilledButton(
          onPressed: () => state.downloadModel(model.id),
          child: Text(downloaded > 0 ? l.modelsResume : l.modelsDownload),
        ),
      ),
      _ => const SizedBox.shrink(),
    };
  }
}
