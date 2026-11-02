import 'package:flutter/material.dart';

import '../app_state.dart';
import '../data/settings.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';

/// Language, speakers, terms, model and device for a file or the defaults.
class SettingsFields extends StatefulWidget {
  const SettingsFields({super.key, required this.state, required this.settings, required this.onChanged});

  final AppState state;
  final JobSettings settings;
  final ValueChanged<JobSettings> onChanged;

  @override
  State<SettingsFields> createState() => _SettingsFieldsState();
}

class _SettingsFieldsState extends State<SettingsFields> {
  late final _glossary = TextEditingController(text: widget.settings.glossary.join('\n'));

  @override
  void dispose() {
    _glossary.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final settings = widget.settings;
    final state = widget.state;
    final models = state.readyTranscriptionModels;
    final model = settings.transcriptionModel ?? state.recommendedTranscription.id;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 12,
      children: [
        Text(l.language, style: Theme.of(context).textTheme.titleSmall),
        SegmentedButton<String>(
          segments: [
            for (final code in ['auto', 'ko', 'en']) ButtonSegment(value: code, label: Text(languageName(l, code))),
          ],
          selected: {settings.language},
          onSelectionChanged: (value) => widget.onChanged(settings.copyWith(language: value.first)),
        ),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: Text(l.diarize),
          value: settings.diarize,
          onChanged: (value) => widget.onChanged(settings.copyWith(diarize: value)),
        ),
        TextField(
          controller: _glossary,
          minLines: 1,
          maxLines: 4,
          decoration: InputDecoration(labelText: l.glossary, helperText: l.glossaryHint, helperMaxLines: 2),
          onChanged: (text) => widget.onChanged(settings.copyWith(glossary: parseGlossary(text))),
        ),
        if (models.isNotEmpty)
          DropdownButtonFormField<String>(
            isExpanded: true,
            initialValue: models.any((m) => m.id == model) ? model : null,
            decoration: InputDecoration(labelText: l.transcriptionModel),
            items: [for (final m in models) DropdownMenuItem(value: m.id, child: Text(modelName(m)))],
            onChanged: (id) => widget.onChanged(settings.copyWith(transcriptionModel: () => id)),
          ),
        if (state.gpus.isNotEmpty)
          DropdownButtonFormField<int>(
            isExpanded: true,
            initialValue: settings.gpuIndex ?? -1,
            decoration: InputDecoration(labelText: l.device),
            items: [
              DropdownMenuItem(value: -1, child: Text(l.deviceCpu)),
              for (final (i, gpu) in state.gpus.indexed)
                DropdownMenuItem(
                  value: i,
                  child: Text(
                    state.usesAccelerator(settings.transcriptionModel)
                        ? l.deviceWithNeuralEngine(gpu.description)
                        : gpu.description,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
            ],
            onChanged: (index) =>
                widget.onChanged(settings.copyWith(gpuIndex: () => index == null || index < 0 ? null : index)),
          ),
      ],
    );
  }
}

/// "Automatic · Tell speakers apart · large-v3 turbo / q5_0"
String settingsSummary(AppLocalizations l, AppState state, JobSettings settings) {
  final model = state.catalog.where((m) => m.id == (settings.transcriptionModel ?? state.recommendedTranscription.id));
  return [
    languageName(l, settings.language),
    settings.diarize ? l.diarize : l.diarizeOff,
    if (model.isNotEmpty) modelName(model.first),
    if (state.gpus.isNotEmpty)
      settings.gpuIndex == null ? l.deviceCpu : state.gpus[settings.gpuIndex!.clamp(0, state.gpus.length - 1)].name,
  ].join(' · ');
}
