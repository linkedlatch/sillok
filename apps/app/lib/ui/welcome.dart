import 'package:flutter/material.dart';

import '../app_state.dart';
import '../l10n/app_localizations.dart';
import 'model_cards.dart';

/// The first start: what Sillok does, and the transcription models to choose
/// one from and download.
class WelcomePage extends StatelessWidget {
  const WelcomePage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
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
                  Text(l.welcomeBody),
                  const SizedBox(height: 8),
                  Text(l.settingsModelsHint, style: Theme.of(context).textTheme.bodySmall),
                  const SizedBox(height: 16),
                  ModelCards(state: state),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
