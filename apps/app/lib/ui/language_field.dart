import 'package:flutter/material.dart';

import '../l10n/app_localizations.dart';
import 'format.dart';
import 'languages.dart';

/// The language to transcribe in: automatic, the system's language, English,
/// or any other of the model's [languages] from a list. The last segment
/// keeps the language last chosen from the list: tapping it chooses that
/// language, tapping it again or its arrow opens the list.
class LanguageField extends StatelessWidget {
  const LanguageField({
    super.key,
    required this.value,
    required this.listed,
    required this.languages,
    required this.systemLocale,
    required this.onChanged,
  });

  /// "auto" or a language code.
  final String value;

  /// The language last chosen from the list, or null.
  final String? listed;
  final List<String> languages;
  final Locale systemLocale;

  /// The language to transcribe in, and the one kept from the list.
  final void Function(String value, String? listed) onChanged;

  static const _list = '';

  /// The segments before the list's: English last, the system's language
  /// before it unless it is English or the model does not know it.
  List<String> get presets {
    final system = systemLanguage(systemLocale, languages);
    return ['auto', if (system != null && system != 'en') system, if (languages.contains('en')) 'en'];
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final presets = this.presets;
    // The list's segment: the chosen language when it is not one of the
    // others, else the one last chosen from the list.
    final kept = !presets.contains(value)
        ? value
        : listed != null && !presets.contains(listed) && languages.contains(listed)
        ? listed
        : null;

    Future<void> openList() async {
      final code = await showDialog<String>(
        context: context,
        builder: (context) => _LanguageDialog(languages: languages, value: value),
      );
      if (code != null) onChanged(code, presets.contains(code) ? listed : code);
    }

    Widget label(String text) => Text(text, maxLines: 1, overflow: TextOverflow.ellipsis);
    return SegmentedButton<String>(
      // Tapping the selected segment gives no selection: for the list's, it
      // opens the list again.
      emptySelectionAllowed: true,
      segments: [
        for (final code in presets) ButtonSegment(value: code, label: label(languageName(l, code))),
        // The arrow follows the text, as a selected segment's icon is the
        // check, and opens the list by itself.
        ButtonSegment(
          value: _list,
          label: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Flexible(child: label(kept != null ? languageName(l, kept) : l.languageOther)),
              GestureDetector(onTap: openList, child: const Icon(Icons.arrow_drop_down, size: 18)),
            ],
          ),
        ),
      ],
      selected: {presets.contains(value) ? value : _list},
      onSelectionChanged: (selection) {
        if (selection.isEmpty) {
          if (!presets.contains(value)) openList();
        } else if (selection.first != _list) {
          onChanged(selection.first, listed);
        } else if (kept != null) {
          onChanged(kept, kept);
        } else {
          openList();
        }
      },
    );
  }
}

class _LanguageDialog extends StatefulWidget {
  const _LanguageDialog({required this.languages, required this.value});

  final List<String> languages;
  final String value;

  @override
  State<_LanguageDialog> createState() => _LanguageDialogState();
}

class _LanguageDialogState extends State<_LanguageDialog> {
  final _search = TextEditingController();

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context);
    final query = _search.text.trim().toLowerCase();
    // By name in the app's language. A search matches that name, the English
    // one, the language's own or its code.
    bool matches(String code) => [
      languageName(l, code),
      languageNameOf(code, 'en'),
      languageSelfName(code) ?? '',
    ].any((name) => name.toLowerCase().contains(query));
    final shown = [
      for (final code in widget.languages)
        if (query.isEmpty || code == query || matches(code)) code,
    ]..sort((a, b) => languageName(l, a).compareTo(languageName(l, b)));
    return AlertDialog(
      title: Text(l.languageChoose),
      content: SizedBox(
        width: 360,
        height: 420,
        child: Column(
          children: [
            TextField(
              controller: _search,
              autofocus: true,
              decoration: InputDecoration(prefixIcon: const Icon(Icons.search), hintText: l.languageSearch),
              onChanged: (_) => setState(() {}),
            ),
            Expanded(
              child: ListView(
                children: [
                  for (final code in shown)
                    ListTile(
                      title: Text(languageName(l, code)),
                      // Its own name, when that reads differently.
                      subtitle: switch (languageSelfName(code)) {
                        final self? when self != languageName(l, code) => Text(self),
                        _ => null,
                      },
                      selected: code == widget.value,
                      trailing: code == widget.value ? const Icon(Icons.check) : null,
                      onTap: () => Navigator.pop(context, code),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(MaterialLocalizations.of(context).cancelButtonLabel),
        ),
      ],
    );
  }
}
