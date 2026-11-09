import 'package:flutter/material.dart';

import '../app_state.dart';
import '../data/settings.dart';
import '../l10n/app_localizations.dart';
import 'format.dart';
import 'language_field.dart';

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
    final languages = _languages(state, model);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 12,
      children: [
        Text(l.language, style: Theme.of(context).textTheme.titleSmall),
        LanguageField(
          value: settings.language,
          listed: settings.listedLanguage,
          languages: languages,
          systemLocale: View.of(context).platformDispatcher.locale,
          onChanged: (code, listed) =>
              widget.onChanged(settings.copyWith(language: code, listedLanguage: () => listed)),
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
            // A language the new model does not know goes back to automatic.
            onChanged: (id) => widget.onChanged(
              settings.copyWith(
                transcriptionModel: () => id,
                language: _languages(state, id ?? state.recommendedTranscription.id).contains(settings.language)
                    ? null
                    : 'auto',
              ),
            ),
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
                  child: Text(gpu.description, overflow: TextOverflow.ellipsis),
                ),
            ],
            onChanged: (index) =>
                widget.onChanged(settings.copyWith(gpuIndex: () => index == null || index < 0 ? null : index)),
          ),
        // Apple builds, once the model's NPU encoder (Neural Engine) is here.
        if (state.hasAccelerator(settings.transcriptionModel))
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: Text(l.npu),
            subtitle: Text(l.npuHint),
            value: settings.npu,
            onChanged: (value) => widget.onChanged(settings.copyWith(npu: value)),
          ),
      ],
    );
  }
}

/// The languages transcription model [id] knows.
List<String> _languages(AppState state, String id) =>
    state.transcriptionModels.where((m) => m.id == id).firstOrNull?.languages ??
    state.recommendedTranscription.languages;

/// "Automatic · Tell speakers apart · large-v3 turbo / q5_0"
String settingsSummary(AppLocalizations l, AppState state, JobSettings settings) {
  final model = state.catalog.where((m) => m.id == (settings.transcriptionModel ?? state.recommendedTranscription.id));
  return [
    languageName(l, settings.language),
    settings.diarize ? l.diarize : l.diarizeOff,
    if (model.isNotEmpty) modelName(model.first),
    if (state.gpus.isNotEmpty) _deviceName(l, state, settings),
  ].join(' · ');
}

/// "CPU", "Apple A18 Pro GPU", or either "+ NPU" when it runs the
/// encoder.
String _deviceName(AppLocalizations l, AppState state, JobSettings settings) {
  final device = settings.gpuIndex == null
      ? l.deviceCpu
      : state.gpus[settings.gpuIndex!.clamp(0, state.gpus.length - 1)].name;
  return settings.npu && state.hasAccelerator(settings.transcriptionModel) ? l.deviceWithNpu(device) : device;
}
