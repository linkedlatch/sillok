import 'package:flutter/material.dart';

import '../app_state.dart';
import '../engine/sillok.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'model_cards.dart';
import 'settings_fields.dart';

/// Defaults for new files, the transcription models, storage and the rest.
class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key, required this.state});

  final AppState state;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  late Future<(int, int)> _sizes = _measure();

  Future<(int, int)> _measure() async => (await widget.state.modelsSize(), await widget.state.store.size());

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final state = widget.state;
    final heading = Theme.of(context).textTheme.titleMedium;
    return Scaffold(
      appBar: AppBar(title: Text(l.settings)),
      body: ListenableBuilder(
        listenable: state,
        builder: (context, _) => SafeArea(
          top: false,
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 720),
              child: ListView(
                padding: const EdgeInsets.all(16),
                children: [
                  Text(l.settingsDefaults, style: heading),
                  const SizedBox(height: 8),
                  SettingsFields(state: state, settings: state.defaults, onChanged: state.setDefaults),
                  const SizedBox(height: 24),
                  Text(l.settingsModels, style: heading),
                  const SizedBox(height: 4),
                  Text(l.settingsModelsHint, style: Theme.of(context).textTheme.bodySmall),
                  const SizedBox(height: 8),
                  ModelCards(state: state, onChanged: _refresh),
                  const SizedBox(height: 24),
                  Text(l.settingsStorage, style: heading),
                  FutureBuilder(
                    future: _sizes,
                    builder: (context, sizes) => Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        ListTile(
                          contentPadding: EdgeInsets.zero,
                          title: Text(l.storageModels(formatBytes(sizes.data?.$1 ?? 0))),
                        ),
                        ListTile(
                          contentPadding: EdgeInsets.zero,
                          title: Text(l.storageHistory(formatBytes(sizes.data?.$2 ?? 0))),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 24),
                  Text(l.settingsMore, style: heading),
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    title: Text(l.verifyModels),
                    subtitle: Text(l.verifyModelsHint),
                    onTap: () async {
                      await state.verifyModels();
                      _refresh();
                    },
                  ),
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    title: Text(l.about),
                    subtitle: Text(l.version(Sillok.version)),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  void _refresh() => setState(() => _sizes = _measure());
}
